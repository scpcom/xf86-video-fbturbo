/*
 * Copyright (C) 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
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

#include <malloc.h>
#include <sys/ioctl.h>

#include "xorgVersion.h"
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "sunxi_disp.h"
#include "sunxi_disp_hwcursor.h"
#include "sunxi_disp_ioctl.h"
#include "sunxi_mali_ump_dri2.h"

#define FBTURBO_DRI2INFOREC_VER 4

/* any point to support earlier? */
#if DRI2INFOREC_VERSION < FBTURBO_DRI2INFOREC_VER
#       error "Requires newer DRI2"
#endif

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static inline uint32_t
crc32_byte(uint32_t crc32, uint8_t data)
{
    crc32 ^= 0xFFFFFFFF;
    crc32 = (crc32 >> 8) ^ crc32_table[(crc32 ^ data) & 0xFF];
    return crc32 ^ 0xFFFFFFFF;
}

static uint32_t calc_bo_checksum(BOInfoPtr bo, uint32_t seed)
{
    int i;
    uint8_t *buf = bo->addr + bo->offs;
    uint32_t result = 0;
    uint32_t hi, lo;
    for (i = 0; i < RANDOM_SAMPLES_COUNT; i++) {
        /* LCG pseudorandom number generation */
        seed = seed * 1103515245 + 12345;
        hi = seed & 0xFFFF0000;
        seed = seed * 1103515245 + 12345;
        lo = seed >> 16;
        result = crc32_byte(result, buf[(hi | lo) % bo->size]);
    }
    return result;
}

static void save_bo_checksum(BOInfoPtr bo, uint32_t seed)
{
    bo->has_checksum = TRUE;
    bo->checksum_seed = seed;
    bo->checksum = calc_bo_checksum(bo, seed);
}

static Bool test_bo_checksum(BOInfoPtr bo)
{
    return calc_bo_checksum(bo, bo->checksum_seed) == bo->checksum;
}

static void unref_bo_info(FBTurboBOOps *bo_ops, BOInfoPtr bo);

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof((a)) / sizeof((a)[0]))
#endif

/*
 * The code below is borrowed from "xserver/dix/window.c"
 */

#define BOXES_OVERLAP(b1, b2) \
      (!( ((b1)->x2 <= (b2)->x1)  || \
	( ((b1)->x1 >= (b2)->x2)) || \
	( ((b1)->y2 <= (b2)->y1)) || \
	( ((b1)->y1 >= (b2)->y2)) ) )

static BoxPtr
WindowExtents(WindowPtr pWin, BoxPtr pBox)
{
    pBox->x1 = pWin->drawable.x - wBorderWidth(pWin);
    pBox->y1 = pWin->drawable.y - wBorderWidth(pWin);
    pBox->x2 = pWin->drawable.x + (int) pWin->drawable.width
        + wBorderWidth(pWin);
    pBox->y2 = pWin->drawable.y + (int) pWin->drawable.height
        + wBorderWidth(pWin);
    return pBox;
}

static int
FancyTraverseTree(WindowPtr pWin, VisitWindowProcPtr func, pointer data)
{
    int result;
    WindowPtr pChild;

    if (!(pChild = pWin))
        return WT_NOMATCH;
    while (1) {
        result = (*func) (pChild, data);
        if (result == WT_STOPWALKING)
            return WT_STOPWALKING;
        if ((result == WT_WALKCHILDREN) && pChild->lastChild) {
            pChild = pChild->lastChild;
            continue;
        }
        while (!pChild->prevSib && (pChild != pWin))
            pChild = pChild->parent;
        if (pChild == pWin)
            break;
        pChild = pChild->prevSib;
    }
    return WT_NOMATCH;
}

static int
WindowWalker(WindowPtr pWin, pointer value)
{
    FBTurboMaliDRI2 *mali = (FBTurboMaliDRI2 *)value;

    if (mali->bWalkingAboveOverlayWin) {
        if (pWin->mapped && pWin->realized && pWin->drawable.class != InputOnly) {
            BoxRec sboxrec1;
            BoxPtr sbox1 = WindowExtents(pWin, &sboxrec1);
            BoxRec sboxrec2;
            BoxPtr sbox2 = WindowExtents(mali->pOverlayWin, &sboxrec2);
            if (BOXES_OVERLAP(sbox1, sbox2)) {
                mali->bOverlayWinOverlapped = TRUE;
                DEBUG_STR(2,"overlapped by %p, x=%d, y=%d, w=%d, h=%d", pWin,
                         pWin->drawable.x, pWin->drawable.y,
                         pWin->drawable.width, pWin->drawable.height);
                return WT_STOPWALKING;
            }
        }
    }
    else if (pWin == mali->pOverlayWin) {
        mali->bWalkingAboveOverlayWin = TRUE;
    }

    return WT_WALKCHILDREN;
}

static Bool
canflip(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->NoFlip) {
		/* flipping is disabled by user option */
		return FALSE;
	} else {
		return (pDraw->type == DRAWABLE_WINDOW) &&
				DRI2CanFlip(pDraw);
	}
}

static PixmapPtr
createpix(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	int flags = canflip(pDraw) ? ARMSOC_CREATE_PIXMAP_SCANOUT : CREATE_PIXMAP_USAGE_BACKING_PIXMAP;
	return pScreen->CreatePixmap(pScreen,
			pDraw->width, pDraw->height, pDraw->depth, flags);
}

/* create the BO */
_X_EXPORT void *
FBTurboCreatePixmap2(ScreenPtr pScreen, int width, int height,
		int depth, int usage_hint, int bitsPerPixel,
		int *new_fb_pitch)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    BOInfoPtr bo;
    int pitch = 0;
    size_t size;

    bo = calloc(1, sizeof(BOInfoRec));
    if (!bo) {
        ERROR_STR("%s: calloc failed", __func__);
        return NULL;
    }
    bo->refcount = 1;
    bo->pPixmap = NULL;

    bo->addr = NULL;
    bo->handle = FBTURBO_BO_INVALID_HANDLE;

    if ((width > 0) && (height > 0)) {
        bo->handle = fPtr->bo_ops->new(fPtr->bo_dev,
                                width,
                                height,
                                depth,
                                bitsPerPixel,
                                usage_hint,
                                FBTURBO_BO_TYPE_DEFAULT);

        if (!fPtr->bo_ops->valid(bo->handle)) {
            ERROR_STR("%s: fPtr->bo_ops->new failed", __func__);
            free(bo);
            return NULL;
        }
    
        pitch = fPtr->bo_ops->get_pitch(bo->handle);

        bo->addr = fPtr->bo_ops->map(bo->handle);
    }

    size = pitch * height;

    bo->size = size;
    bo->depth = depth;
    bo->width = width;
    bo->height = height;
    bo->bpp = bitsPerPixel;
    bo->usage_hint = usage_hint;

    *new_fb_pitch = pitch;

    return bo;
}

_X_EXPORT void
FBTurboDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr bo = driverPriv;

	if (!driverPriv)
		return;

	if (bo->handle != fPtr->scanout)
		unref_bo_info(mali->bo_ops, driverPriv);
}

static BOInfoPtr MigratePixmapToBO(PixmapPtr pPixmap);

_X_EXPORT Bool
FBTurboModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
		int depth, int bitsPerPixel, int devKind,
		pointer pPixData)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr bo = FBTurboGetPixmapDriverPrivate(pPixmap);
	size_t datasize;

	if (!bo) {
		bo = MigratePixmapToBO(pPixmap);
		//return FALSE;
	}

	/* Only modify specified fields, keeping all others intact. */
	if (pPixData)
		pPixmap->devPrivate.ptr = pPixData;

	if (devKind > 0)
		pPixmap->devKind = devKind;

	/*
	 * Someone is messing with the memory allocation. Let's step out of
	 * the picture.
	 */
	
	if (pPixData && pPixData != bo->addr) {
	    if (pPixData == fPtr->scanout_ptr) {
		INFO_MSG("%s: create scanout", __func__);
	    }
	    else {
		if (mali->bo_ops->valid(bo->handle)) {
			mali->bo_ops->unmap(bo->handle);
			mali->bo_ops->release(bo->handle);
			free(bo->handle);
		}
		bo->addr = NULL;
		bo->size = 0;
		bo->handle = FBTURBO_BO_INVALID_HANDLE;

		/* Returning FALSE calls miModifyPixmapHeader */
		return FALSE;
	    }
	}

	if (depth > 0)
		pPixmap->drawable.depth = depth;

	if (bitsPerPixel > 0)
		pPixmap->drawable.bitsPerPixel = bitsPerPixel;

	if (width > 0)
		pPixmap->drawable.width = width;

	if (height > 0)
		pPixmap->drawable.height = height;

	/* Replacing the pixmap's current bo with the scanout bo */
	if (pPixData && pPixData == fPtr->scanout_ptr && bo->handle != fPtr->scanout) {
		FBTurboBOHandle old_bo = bo->handle;

		INFO_MSG("%s: set bo to scanout", __func__);

		bo->handle = fPtr->scanout;
		bo->size = mali->bo_ops->get_pitch(bo->handle) * pPixmap->drawable.height;
		bo->addr = fPtr->scanout_ptr;

		bo->depth = pPixmap->drawable.depth;
		bo->width = pPixmap->drawable.width;
		bo->height = pPixmap->drawable.height;
		bo->bpp = pPixmap->drawable.bitsPerPixel;

		/* pixmap takes a ref on its new bo */
		mali->bo_ops->hold(bo->handle);

		if (old_bo) {
			/* We are detaching the old_bo so clear it now. */
			if (mali->bo_ops->has_dmabuf(old_bo))
				mali->bo_ops->clear_dmabuf(old_bo);
			/* pixmap drops ref on previous bo */
			mali->bo_ops->release(old_bo);
		}
	}

	if (!bo->pPixmap)
		bo->pPixmap = pPixmap;
	else if (bo->pPixmap != pPixmap)
		INFO_MSG("%s: priv=%p, Pixmap %p != %p", __func__, bo, bo->pPixmap, pPixmap);
	
	/*
	 * X will sometimes create an empty pixmap (width/height == 0) and then
	 * use ModifyPixmapHeader to point it at PixData. We'll hit this path
	 * during the CreatePixmap call. Just return true and skip the allocate
	 * in this case.
	 */
	if (!pPixmap->drawable.width || !pPixmap->drawable.height)
		return TRUE;

	datasize = pPixmap->devKind * pPixmap->drawable.height;
	if (bo->addr && bo->addr == fPtr->scanout_ptr)
		datasize = bo->size;

	if (!bo->addr || bo->size != datasize) {
		int pitch;
		size_t size;

		/* re-allocate buffer! */
		if (mali->bo_ops->valid(bo->handle)) {
			mali->bo_ops->unmap(bo->handle);
			mali->bo_ops->release(bo->handle);
			free(bo->handle);
		}
		bo->addr = 0;
		bo->size = 0;
		bo->handle = mali->bo_ops->new(mali->bo_dev,
                                pPixmap->drawable.width,
                                pPixmap->drawable.height,
                                pPixmap->drawable.depth,
                                pPixmap->drawable.bitsPerPixel,
                                bo->usage_hint,
                                FBTURBO_BO_TYPE_DEFAULT);

		if (!mali->bo_ops->valid(bo->handle)) {
			ERROR_STR("%s: mali->bo_ops->new failed", __func__);
			return FALSE;
		}

		pitch = mali->bo_ops->get_pitch(bo->handle);
		size = pitch * pPixmap->drawable.height;

		bo->size = size;
		bo->addr = mali->bo_ops->map(bo->handle);

		if (!bo->addr) {
			ERROR_MSG("failed to allocate %zu bytes mem",
					datasize);
			bo->size = 0;
			return FALSE;
		}

		bo->depth = pPixmap->drawable.depth;
		bo->width = pPixmap->drawable.width;
		bo->height = pPixmap->drawable.height;
		bo->bpp = pPixmap->drawable.bitsPerPixel;

		pPixmap->devKind = pitch;
	}

	FBTurboSetPixmapDriverPrivate(pPixmap, bo);

	return TRUE;
}

_X_EXPORT void
FBTurboWaitMarker(ScreenPtr pScreen, int marker)
{
	/* no-op */
}

_X_EXPORT Bool
FBTurboPrepareAccess(PixmapPtr pPixmap, int index)
{
        ScrnInfoPtr pScrn = pix2scrn(pPixmap);
        FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
        BOInfoPtr bo = FBTurboGetPixmapDriverPrivate(pPixmap);

	if (bo) {
		if (!mali->bo_ops->valid(bo->handle)) {
			INFO_MSG("%s: no handle", __func__);
			return TRUE;
		}
		if (!bo->addr)
			INFO_MSG("%s: no addr", __func__);

		bo->BackupDevKind = pPixmap->devKind;
		bo->BackupDevPrivatePtr = pPixmap->devPrivate.ptr;

		pPixmap->devKind = mali->bo_ops->get_pitch(bo->handle);
		pPixmap->devPrivate.ptr = mali->bo_ops->map(bo->handle);
	}
	else {
		INFO_MSG("%s: no bo", __func__);
	}

	return TRUE;
}

_X_EXPORT void
FBTurboFinishAccess(PixmapPtr pPixmap, int index)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr bo = FBTurboGetPixmapDriverPrivate(pPixmap);

	if (bo) {
		if (!mali->bo_ops->valid(bo->handle))
			return;

		pPixmap->devPrivate.ptr = NULL;

		mali->bo_ops->unmap(bo->handle);
	}
}

_X_EXPORT Bool
FBTurboPixmapIsOffscreen(PixmapPtr pPixmap)
{
        BOInfoPtr bo = FBTurboGetPixmapDriverPrivate(pPixmap);
	Bool ret;

	ret = (bo && bo->addr);

	return ret;
}

/* Migrate pixmap to BO */
static BOInfoPtr
MigratePixmapToBO(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    BOInfoPtr bo;
    int pitch = 0;

    bo = FBTurboGetPixmapDriverPrivate(pPixmap);

    if (bo && mali->bo_ops->valid(bo->handle)) {
        DEBUG_MSG(2,"MigratePixmapToBO %p, already exists = %p", pPixmap, bo);
        return bo;
    }

    if (!bo) {
        bo = FBTurboCreatePixmap2(pScreen,
                 pPixmap->drawable.width, pPixmap->drawable.height,
                 pPixmap->drawable.depth, pPixmap->usage_hint,
                 pPixmap->drawable.bitsPerPixel, &pitch);
    }

    bo->pPixmap = pPixmap;

    /* copy the pixel data to the new location */
    if (pitch == pPixmap->devKind) {
        memcpy(bo->addr, pPixmap->devPrivate.ptr, bo->size);
    } else {
        int y;
        for (y = 0; y < bo->height; y++) {
            memcpy(bo->addr + y * pitch, 
                   (char *)pPixmap->devPrivate.ptr + y * pPixmap->devKind,
                   pPixmap->devKind);
        }
    }

    bo->BackupDevKind = pPixmap->devKind;
    bo->BackupDevPrivatePtr = pPixmap->devPrivate.ptr;

    pPixmap->devKind = pitch;
    pPixmap->devPrivate.ptr = bo->addr;

    FBTurboSetPixmapDriverPrivate(pPixmap, bo);

    DEBUG_MSG(2,"MigratePixmapToBO %p, new buf = %p", pPixmap, bo);
    return bo;
}

static void UpdateOverlay(ScreenPtr pScreen);

static void unref_bo_info(FBTurboBOOps *bo_ops, BOInfoPtr bo)
{
    if (--bo->refcount <= 0) {
	FBTurboBOHandle old_handle = bo->handle;

        DEBUG_STR(2,"unref_bo_info(%p) [refcount=%d, handle=%p]",
                 bo, bo->refcount, bo->handle);

	//FBTurboDelPixmapDriverPrivate(bo->pPixmap, bo);
	bo->pPixmap = NULL;
	bo->addr = NULL;
	bo->size = 0;
	bo->handle = FBTURBO_BO_INVALID_HANDLE;

        if (bo_ops->valid(old_handle)) {
            bo_ops->unmap(old_handle);
            bo_ops->release(old_handle);

            free(old_handle);
        }
        //free(bo);
    }
    else {
        DEBUG_STR(2,"Reduced bo_info %p refcount to %d",
                 bo, bo->refcount);
    }
}

static void bo_add_to_queue(DRI2WindowStatePtr window_state,
                                BOInfoPtr bo)
{
    if (window_state->bo_queue[window_state->bo_queue_head]) {
        ERROR_STR("Fatal error, BOs queue overflow!");
        return;
    }

    window_state->bo_queue[window_state->bo_queue_head] = bo;
    window_state->bo_queue_head = (window_state->bo_queue_head + 1) %
                                   ARRAY_SIZE(window_state->bo_queue);
}

static BOInfoPtr bo_fetch_from_queue(DRI2WindowStatePtr window_state)
{
    BOInfoPtr bo;

    /* Check if the queue is empty */
    if (window_state->bo_queue_tail == window_state->bo_queue_head)
        return NULL;

    bo = window_state->bo_queue[window_state->bo_queue_tail];
    window_state->bo_queue[window_state->bo_queue_tail] = NULL;

    window_state->bo_queue_tail = (window_state->bo_queue_tail + 1) %
                                   ARRAY_SIZE(window_state->bo_queue);
    return bo;
}

/* Verify and fixup the DRI2Buffer before returning it to the X server */
static DRI2Buffer2Ptr validate_dri2buf(DRI2Buffer2Ptr dri2buf)
{
    BOInfoPtr bo = (BOInfoPtr)dri2buf->driverPrivate;
    bo->secure_id = dri2buf->name;
    bo->pitch     = dri2buf->pitch;
    bo->cpp       = dri2buf->cpp;
    bo->offs      = dri2buf->flags;
    return dri2buf;
}

static Bool prepare_flip(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
	ScreenPtr                pScreen  = pDraw->pScreen;
	ScrnInfoPtr              pScrn    = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2         *mali     = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr                privates = (BOInfoPtr)buffer->driverPrivate;

	if (canflip(pDraw) && buffer->attachment != DRI2BufferFrontLeft) {
		/* Create an fb around this buffer. This will fail and we will
		 * fall back to blitting if the display controller hardware
		 * cannot scan out this buffer (for example, if it doesn't
		 * support the format or there was insufficient scanout memory
		 * at buffer creation time). */
		int ret = mali->bo_ops->add_fb(privates->handle);
		if (ret) {
			WARNING_MSG(
					"Falling back to blitting a flippable window");
		}
	}

	DRI2_BUFFER_SET_FB(buffer->flags, mali->bo_ops->get_fb(privates->handle) > 0 ? 1 : 0);
	DRI2_BUFFER_SET_REUSED(buffer->flags, 0);

	return TRUE;
}

static inline PixmapPtr
get_drawable_pixmap(DrawablePtr drawable)
{
    ScreenPtr screen = drawable->pScreen;

    if (drawable->type == DRAWABLE_PIXMAP)
        return (PixmapPtr) drawable;
    else
        return screen->GetWindowPixmap((WindowPtr) drawable);
}

static DRI2Buffer2Ptr
MaliDRI2CreateBuffer2(ScreenPtr pScreen, DrawablePtr  pDraw,
                       unsigned int attachment, unsigned int format)
{
    ScrnInfoPtr              pScrn    = xf86Screens[pScreen->myNum];
    DRI2Buffer2Ptr           buffer;
    BOInfoPtr                privates;
    FBDevPtr                 fPtr = FBDEVPTR(pScrn);
    FBTurboMaliDRI2         *mali = FBTURBO_MALI_DRI2(pScrn);
    sunxi_disp_t            *disp = SUNXI_DISP(pScrn);
    Bool                     can_use_overlay = TRUE;
    PixmapPtr                pPixmap = NULL;
    PixmapPtr                pWindowPixmap;
    DRI2WindowStatePtr       window_state = NULL;
    Bool                     need_window_resize_bug_workaround = FALSE;

    if (!(buffer = calloc(1, sizeof *buffer))) {
        ERROR_MSG("DRI2CreateBuffer: calloc failed");
        return NULL;
    }

    buffer->attachment    = attachment;
    buffer->format        = format;

    if (pDraw->type == DRAWABLE_WINDOW &&
        (pWindowPixmap = pScreen->GetWindowPixmap((WindowPtr)pDraw)))
    {
        DEBUG_MSG(2,"DRI2CreateBuffer: win=%p (w=%d, h=%d, x=%d, y=%d) has backing pix=%p (w=%d, h=%d, screen_x=%d, screen_y=%d)",
                 pDraw, pDraw->width, pDraw->height, pDraw->x, pDraw->y,
                 pWindowPixmap, pWindowPixmap->drawable.width, pWindowPixmap->drawable.height,
                 pWindowPixmap->screen_x, pWindowPixmap->screen_y);
    }

    if (buffer->attachment == DRI2BufferFrontLeft && fPtr->UseEXA) {
        pPixmap = draw2pix(pDraw);
        if (pPixmap && pPixmap->drawable.pScreen != pScreen)
            pPixmap = NULL;
        if (pPixmap)
            pPixmap->refcnt++;
    } 
    else if (pDraw->type == DRAWABLE_PIXMAP) {
        pPixmap = (PixmapPtr)pDraw;
    }

    /* If it is a pixmap, just migrate this pixmap to BO */
    if (pPixmap)
    {
        if (!(privates = MigratePixmapToBO(pPixmap))) {
            ERROR_MSG("DRI2CreateBuffer: MigratePixmapToBO failed");
            free(buffer);
            return NULL;
        }
        privates->refcount++;
        buffer->attachment    = attachment;
        buffer->driverPrivate = privates;
        buffer->format        = format;
        buffer->flags         = 0;
        buffer->cpp           = pPixmap->drawable.bitsPerPixel / 8;
        buffer->pitch         = pPixmap->devKind;
        buffer->name = mali->bo_ops->secure_id_get(privates->handle);

        prepare_flip(pDraw, buffer);

        DEBUG_MSG(2,"DRI2CreateBuffer pix=%p, buf=%p:%p, att=%d, id=%d:%d, w=%zd, h=%zd, cpp=%d, depth=%d",
                 pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
                 privates->width, privates->height, buffer->cpp, privates->depth);

        return validate_dri2buf(buffer);
    }

    if (!(privates = calloc(1, sizeof *privates))) {
        ERROR_MSG("DRI2CreateBuffer: calloc failed");
        free(buffer);
        return NULL;
    }
    privates->refcount = 1;

    /* The default common values */
    buffer->attachment    = attachment;
    buffer->driverPrivate = privates;
    buffer->format        = format + 1; /* hack to suppress DRI2 buffers reuse */
    buffer->flags         = 0;
    buffer->cpp           = pDraw->bitsPerPixel / 8;
    /* Stride must be 8 bytes aligned for Mali400 */
    buffer->pitch         = ((buffer->cpp * pDraw->width + 7) / 8) * 8;

    privates->size     = pDraw->height * buffer->pitch;
    privates->width    = pDraw->width;
    privates->height   = pDraw->height;
    privates->depth    = pDraw->depth;
    privates->bpp      = pDraw->bitsPerPixel;
    privates->usage_hint = CREATE_PIXMAP_USAGE_BACKING_PIXMAP;

    /* We are not interested in anything other than back buffer requests ... */
    if (attachment != DRI2BufferBackLeft || pDraw->type != DRAWABLE_WINDOW) {
        /* ... and just return some dummy BO */
        privates->handle = FBTURBO_BO_INVALID_HANDLE;
        privates->addr   = NULL;
        buffer->name     = mali->bo_null_secure_id;
        return validate_dri2buf(buffer);
    }

    /* We could not allocate disp layer or get framebuffer secure id */
    if (!disp || mali->ump_fb_secure_id == FBTURBO_BO_INVALID_SECURE_ID)
        can_use_overlay = FALSE;

    /* Overlay is already used by a different window */
    if (mali->pOverlayWin && mali->pOverlayWin != (void *)pDraw)
        can_use_overlay = FALSE;

    /* Don't waste overlay on some strange 1x1 window created by gnome-shell */
    if (pDraw->width == 1 && pDraw->height == 1)
        can_use_overlay = FALSE;

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32 && pDraw->bitsPerPixel != 16)
        can_use_overlay = FALSE;

    if (disp && disp->framebuffer_size - disp->gfx_layer_size < privates->size * 2) {
        DEBUG_MSG(2,"DRI2CreateBuffer: Not enough space in the offscreen framebuffer (wanted %zd for DRI2)",
                 privates->size);
        can_use_overlay = FALSE;
    }

    /* Allocate the DRI2-related window bookkeeping information */
    window_state = FBTurboGetWindowDriverPrivate(pDraw);
    if (!window_state) {
        window_state = calloc(1, sizeof(*window_state));
        window_state->pDraw = pDraw;
        FBTurboSetWindowDriverPrivate(pDraw, window_state);
        DEBUG_MSG(2,"DRI2CreateBuffer: Allocate DRI2 bookkeeping for window %p", pDraw);
        if (disp && can_use_overlay) {
            /* erase the offscreen part of the framebuffer */
            memset(disp->framebuffer_addr + disp->gfx_layer_size, 0,
                   disp->framebuffer_size - disp->gfx_layer_size);
        }
    }
    window_state->buf_request_cnt++;

    /* For odd buffer requests save the window size */
    if (window_state->buf_request_cnt & 1) {
        /* remember window size for one buffer */
        window_state->width = pDraw->width;
        window_state->height = pDraw->height;
    }

    /* For even buffer requests check if the window size is still the same */
    need_window_resize_bug_workaround =
                          !(window_state->buf_request_cnt & 1) &&
                          (pDraw->width != window_state->width ||
                           pDraw->height != window_state->height) &&
                          mali->bo_null_secure_id <= 2;

    if (can_use_overlay) {
        /* Release unneeded buffers */
        if (window_state->bo_mem_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_mem_ptr);
        window_state->bo_mem_ptr = NULL;

        /* Use offscreen part of the framebuffer as an overlay */
        privates->handle = FBTURBO_BO_INVALID_HANDLE;
        privates->addr = disp->framebuffer_addr;

        buffer->name = mali->ump_fb_secure_id;

        if (window_state->buf_request_cnt & 1) {
            buffer->flags = disp->gfx_layer_size;
            privates->extra_flags |= BO_MUST_BE_ODD_FRAME;
        }
        else {
            buffer->flags = disp->gfx_layer_size + privates->size;
            privates->extra_flags |= BO_MUST_BE_EVEN_FRAME;
        }

        bo_add_to_queue(window_state, privates);
        privates->refcount++;

        mali->pOverlayWin = (WindowPtr)pDraw;

        if (need_window_resize_bug_workaround) {
            DEBUG_MSG(2,"DRI2CreateBuffer: DRI2 buffers size mismatch detected, trying to recover");
            buffer->name = mali->ump_alternative_fb_secure_id;
        }
    }
    else {
        /* Release unneeded buffers */
        if (window_state->bo_back_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_back_ptr);
        window_state->bo_back_ptr = NULL;
        if (window_state->bo_front_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_front_ptr);
        window_state->bo_front_ptr = NULL;

        if (need_window_resize_bug_workaround) {
            DEBUG_MSG(2,"DRI2CreateBuffer: DRI2 buffers size mismatch detected, trying to recover");

            if (window_state->bo_mem_ptr)
                unref_bo_info(mali->bo_ops, window_state->bo_mem_ptr);
            window_state->bo_mem_ptr = NULL;

            privates->handle = FBTURBO_BO_INVALID_HANDLE;
            privates->addr   = NULL;
            buffer->name     = mali->bo_null_secure_id;
            return validate_dri2buf(buffer);
        }

        /* Reuse the existing BO if we can */
        if (window_state->bo_mem_ptr &&
            window_state->bo_mem_ptr->size == privates->size &&
            window_state->bo_mem_ptr->depth == privates->depth &&
            window_state->bo_mem_ptr->width == privates->width &&
            window_state->bo_mem_ptr->height == privates->height) {

            free(privates);

            privates = window_state->bo_mem_ptr;
            privates->refcount++;
            buffer->driverPrivate = privates;
            buffer->name = mali->bo_ops->secure_id_get(privates->handle);

            DEBUG_MSG(2,"DRI2CreateBuffer: Reuse the already allocated BO %p, id=%d",
                     privates, buffer->name);
            return validate_dri2buf(buffer);
        }

        /* Allocate BO */
        if (fPtr->UseDumb) {
            pPixmap = createpix(pDraw);

            privates = MigratePixmapToBO(pPixmap);

            privates->pPixmap = pPixmap;

            buffer->driverPrivate = privates;
        }
        else
            privates->handle = mali->bo_ops->new(mali->bo_dev,
                                privates->width,
                                privates->height,
                                privates->depth,
                                privates->bpp,
                                privates->usage_hint,
                                FBTURBO_BO_TYPE_USE_CACHE);

	mali->bo_ops->switch_hw_usage(privates->handle, FALSE);

        if (!mali->bo_ops->valid(privates->handle)) {
            ERROR_MSG("DRI2CreateBuffer: Failed to allocate BO (size=%d)",
                   (int)privates->size);
        }

        if (!privates->addr)
            privates->addr = mali->bo_ops->map(privates->handle);

        buffer->name = mali->bo_ops->secure_id_get(privates->handle);
        buffer->flags = 0;

        /* Replace the old BO with the newly allocated one */
        if (window_state->bo_mem_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_mem_ptr);

        window_state->bo_mem_ptr = privates;
        privates->refcount++;

        prepare_flip(pDraw, buffer);
    }

    DEBUG_MSG(2,"DRI2CreateBuffer win=%p, buf=%p:%p, att=%d, id=%d:%d, w=%zd, h=%zd, cpp=%d, depth=%d",
             pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
             privates->width, privates->height, buffer->cpp, privates->depth);

    return validate_dri2buf(buffer);
}

static DRI2Buffer2Ptr
MaliDRI2CreateBuffer(DrawablePtr  pDraw, unsigned int attachment,
                      unsigned int format)
{
    return MaliDRI2CreateBuffer2(pDraw->pScreen, pDraw, attachment,
                                  format);
}

static void MaliDRI2DestroyBuffer2(ScreenPtr pScreen, DrawablePtr pDraw,
                                    DRI2Buffer2Ptr buffer)
{
    BOInfoPtr privates;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);

    if (mali->pOverlayDirtyBO == buffer->driverPrivate)
        mali->pOverlayDirtyBO = NULL;

    DEBUG_MSG(2,"DRI2DestroyBuffer %s=%p, buf=%p:%p, att=%d",
             pDraw->type == DRAWABLE_WINDOW ? "win" : "pix",
             pDraw, buffer, buffer->driverPrivate, buffer->attachment);

    if (buffer != NULL) {
        privates = (BOInfoPtr)buffer->driverPrivate;
        unref_bo_info(mali->bo_ops, privates);
        free(buffer);
    }
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    MaliDRI2DestroyBuffer2(pDraw->pScreen, pDraw, buffer);
}

/* Do ordinary copy */
static void MaliDRI2CopyRegion2_copy(ScreenPtr        pScreen,
                                    DrawablePtr      pDraw,
                                    RegionPtr        pRegion,
                                    DrawablePtr      pDstDraw,
                                    Bool             translate,
                                    FBTurboBOOps     *bo_ops,
                                    BOInfoPtr        bo)
{
    GCPtr pGC;
    RegionPtr copyRegion;
    PixmapPtr pScratchPixmap;
    DrawablePtr pSrcDraw = NULL;
    int off_x = 0, off_y = 0;

    if (bo_ops->valid(bo->handle)) {
        /* That's a normal BO allocation, not a wrapped framebuffer */
        bo_ops->switch_hw_usage(bo->handle, TRUE);
    }

    if (translate && pDraw->type == DRAWABLE_WINDOW) {
#ifdef COMPOSITE
        PixmapPtr pixmap = get_drawable_pixmap(pDraw);
        off_x = -pixmap->screen_x;
        off_y = -pixmap->screen_y;
#endif
        off_x += pDraw->x;
        off_y += pDraw->y;
    }

    pGC = GetScratchGC(pDstDraw->depth, pScreen);

    pScratchPixmap = GetScratchPixmapHeader(pScreen,
                                            bo->width, bo->height,
                                            bo->depth, bo->cpp * 8,
                                            bo->pitch,
                                            bo->addr + bo->offs);
    pSrcDraw = (DrawablePtr)pScratchPixmap;

    copyRegion = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, copyRegion, pRegion);
    if (translate)
        REGION_TRANSLATE(pScreen, copyRegion, off_x, off_y);
    (*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0);
    ValidateGC(pDstDraw, pGC);

    (*pGC->ops->CopyArea)(pSrcDraw, pDstDraw, pGC, 0, 0,
                          pDraw->width, pDraw->height, 0, 0);

    FreeScratchPixmapHeader(pScratchPixmap);
    FreeScratchGC(pGC);

    if (bo_ops->valid(bo->handle)) {
        /* That's a normal BO allocation, not a wrapped framebuffer */
        bo_ops->switch_hw_usage(bo->handle, FALSE);
    }
}

static void MaliDRI2CopyRegion_copy(DrawablePtr      pDraw,
                                     RegionPtr        pRegion,
                                     FBTurboBOOps     *bo_ops,
                                     BOInfoPtr        bo)
{
    MaliDRI2CopyRegion2_copy(pDraw->pScreen, pDraw, pRegion, pDraw, FALSE, bo_ops, bo);
}

static void FlushOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);

    if (mali->pOverlayWin && mali->pOverlayDirtyBO) {
        DEBUG_MSG(2,"Flushing overlay content from DRI2 buffer to window");
        MaliDRI2CopyRegion_copy((DrawablePtr)mali->pOverlayWin,
                                &pScreen->root->winSize,
                                mali->bo_ops,
                                mali->pOverlayDirtyBO);
        mali->pOverlayDirtyBO = NULL;
    }
}

#ifdef DEBUG_WITH_RGB_PATTERN
static void check_rgb_pattern(DRI2WindowStatePtr window_state,
                              BOInfoPtr bo)
{
    switch (*(uint32_t *)(bo->addr + bo->offs)) {
    case 0xFFFF0000:
        if (window_state->rgb_pattern_state == 0) {
            ERROR_STR("starting RGB pattern with [Red]");
        }
        else if (window_state->rgb_pattern_state != 'B') {
            ERROR_STR("warning - transition to [Red] not from [Blue]");
        }
        window_state->rgb_pattern_state = 'R';
        break;
    case 0xFF00FF00:
        if (window_state->rgb_pattern_state == 0) {
            ERROR_STR("starting RGB pattern with [Green]");
        }
        else if (window_state->rgb_pattern_state != 'R') {
            ERROR_STR("warning - transition to [Green] not from [Red]");
        }
        window_state->rgb_pattern_state = 'G';
        break;
    case 0xFF0000FF:
        if (window_state->rgb_pattern_state == 0) {
            ERROR_STR("starting RGB pattern with [Blue]");
        }
        else if (window_state->rgb_pattern_state != 'G') {
            ERROR_STR("warning - transition to [Blue] not from [Green]");
        }
        window_state->rgb_pattern_state = 'B';
        break;
    default:
        if (window_state->rgb_pattern_state != 0) {
            ERROR_STR("stopping RGB pattern");
        }
        window_state->rgb_pattern_state = 0;
    }
}
#endif

static void
MaliDRI2CopyRegion2(ScreenPtr pScreen, DrawablePtr pDraw, RegionPtr pRegion,
                     DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    BOInfoPtr bo;
    sunxi_disp_t *disp = SUNXI_DISP(xf86Screens[pScreen->myNum]);
    DRI2WindowStatePtr window_state = FBTurboGetWindowDriverPrivate(pDraw);
    DrawablePtr pDstDraw = pDraw;
    Bool translate = FALSE;

    if (pDstBuffer->attachment == DRI2BufferFrontLeft &&
             pDraw->pScreen != pScreen) {
        pDstDraw = DRI2UpdatePrime(pDraw, pDstBuffer);
        if (!pDstDraw)
            return;
        if (pDstDraw != pDraw)
            translate = TRUE;
    }

    if (pDraw->type == DRAWABLE_PIXMAP) {
        DEBUG_MSG(2,"MaliDRI2CopyRegion has been called for pixmap %p", pDraw);
        return;
    }

    if (!window_state) {
        DEBUG_MSG(2,"MaliDRI2CopyRegion: can't find window %p in the hash", pDraw);
        return;
    }

    /* Try to fetch a new BO from the queue */
    bo = bo_fetch_from_queue(window_state);

    /*
     * In the case if the queue of incoming buffers is already empty and
     * we are just swapping two allocated DRI2 buffers, we can do an extra
     * sanity check. The normal behaviour is that the back buffer may change,
     * and the front buffer may not. But if this is not the case, we need to
     * take some corrective actions here.
     */
    if (!bo && window_state->bo_front_ptr &&
                   window_state->bo_front_ptr->addr &&
                   window_state->bo_back_ptr &&
                   window_state->bo_back_ptr->addr &&
                   window_state->bo_front_ptr != window_state->bo_back_ptr) {

        BOInfoPtr bo_front = window_state->bo_front_ptr;
        BOInfoPtr bo_back = window_state->bo_back_ptr;

        Bool front_modified = bo_front->has_checksum && !test_bo_checksum(bo_front);
        Bool back_modified = bo_back->has_checksum && !test_bo_checksum(bo_back);

        bo_front->has_checksum = FALSE;
        bo_back->has_checksum = FALSE;

        if (back_modified && !front_modified) {
            /* That's normal, we have successfully passed this check */
            bo_back->extra_flags |= BO_PASSED_ORDER_CHECK;
            bo_front->extra_flags |= BO_PASSED_ORDER_CHECK;
        }
        else if (front_modified) {
            /* That's bad, the order of buffers is messed up, but we can exchange them */
            BOInfoPtr tmp              = window_state->bo_back_ptr;
            window_state->bo_back_ptr  = window_state->bo_front_ptr;
            window_state->bo_front_ptr = tmp;
            DEBUG_MSG(2,"Unexpected modification of the front buffer detected.");
        }
        else {
            /* Not enough information to make a decision yet */
        }
    }

    /*
     * Swap back and front buffers. But also ensure that the buffer
     * flags BO_MUST_BE_ODD_FRAME and BO_MUST_BE_EVEN_FRAME
     * are respected. In the case if swapping the buffers would result
     * in fetching the BO from the queue in the wrong order,
     * just skip the swap. This is a hack, which causes some temporary
     * glitch when resizing windows, but prevents a bigger problem.
     */
    if (!bo || (!((bo->extra_flags & BO_MUST_BE_ODD_FRAME) &&
                     (window_state->buf_swap_cnt & 1)) &&
                    !((bo->extra_flags & BO_MUST_BE_EVEN_FRAME) &&
                     !(window_state->buf_swap_cnt & 1)))) {
        BOInfoPtr tmp              = window_state->bo_back_ptr;
        window_state->bo_back_ptr  = window_state->bo_front_ptr;
        window_state->bo_front_ptr = tmp;
        window_state->buf_swap_cnt++;
    }

    /* Try to replace the front buffer with a new BO from the queue */
    if (bo) {
        if (window_state->bo_front_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_front_ptr);
        window_state->bo_front_ptr = bo;
    }
    else {
        bo = window_state->bo_front_ptr;
    }

    if (!bo)
        bo = window_state->bo_mem_ptr;

    if (!bo || !bo->addr)
        return;

#ifdef DEBUG_WITH_RGB_PATTERN
    check_rgb_pattern(window_state, bo);
#endif

    /*
     * Here we can calculate checksums over randomly sampled bytes from BO
     * in order to check later whether they had been modified. This
     * is skipped if the buffers have BO_PASSED_ORDER_CHECK flag set.
     */
    if (window_state->bo_front_ptr && window_state->bo_front_ptr->addr &&
        window_state->bo_back_ptr && window_state->bo_back_ptr->addr &&
        window_state->bo_front_ptr != window_state->bo_back_ptr) {

        BOInfoPtr bo_front = window_state->bo_front_ptr;
        BOInfoPtr bo_back = window_state->bo_back_ptr;

        if (!(bo_front->extra_flags & BO_PASSED_ORDER_CHECK))
            save_bo_checksum(bo_front, window_state->buf_swap_cnt);
        if (!(bo_back->extra_flags & BO_PASSED_ORDER_CHECK))
            save_bo_checksum(bo_back, window_state->buf_swap_cnt);
    }

    UpdateOverlay(pScreen);

    if (!mali->bOverlayWinEnabled || mali->bo_ops->valid(bo->handle)) {
        MaliDRI2CopyRegion2_copy(pScreen, pDraw, pRegion, pDstDraw, translate, mali->bo_ops, bo);
        mali->pOverlayDirtyBO = NULL;
        return;
    }

    /* Mark the overlay as "dirty" and remember the last up to date BO */
    mali->pOverlayDirtyBO = bo;

    /* Activate the overlay */
    sunxi_layer_set_output_window(disp, pDraw->x, pDraw->y, pDraw->width, pDraw->height);
    sunxi_layer_set_rgb_input_buffer(disp, bo->cpp * 8, bo->offs,
                                     bo->width, bo->height, bo->pitch / 4);
    sunxi_layer_show(disp);

    if (mali->bSwapbuffersWait) {
        /* FIXME: blocking here for up to 1/60 second is not nice */
        sunxi_wait_for_vsync(disp);
    }
}

static void
MaliDRI2CopyRegion(DrawablePtr pDraw, RegionPtr pRegion,
                    DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer)
{
    MaliDRI2CopyRegion2(pDraw->pScreen, pDraw, pRegion, pDstBuffer,
                         pSrcBuffer);
}

/************************************************************************/

static void UpdateOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);

    if (!mali->pOverlayWin || !disp)
        return;

    /* Disable overlays if the hardware cursor is not in use */
    if (!mali->bHardwareCursorIsInUse) {
        if (mali->bOverlayWinEnabled) {
            DEBUG_MSG(2,"Disabling overlay (no hardware cursor)");
            sunxi_layer_hide(disp);
            mali->bOverlayWinEnabled = FALSE;
        }
        return;
    }

    /* If the window is not mapped, make sure that the overlay is disabled */
    if (!mali->pOverlayWin->mapped)
    {
        if (mali->bOverlayWinEnabled) {
            DEBUG_MSG(2,"Disabling overlay (window is not mapped)");
            sunxi_layer_hide(disp);
            mali->bOverlayWinEnabled = FALSE;
        }
        return;
    }

    /*
     * Walk the windows tree to get the obscured/unobscured status of
     * the window (because we can't rely on self->pOverlayWin->visibility
     * for redirected windows).
     */

    mali->bWalkingAboveOverlayWin = FALSE;
    mali->bOverlayWinOverlapped = FALSE;
    FancyTraverseTree(pScreen->root, WindowWalker, mali);

    /* If the window got overlapped -> disable overlay */
    if (mali->bOverlayWinOverlapped && mali->bOverlayWinEnabled) {
        DEBUG_MSG(2,"Disabling overlay (window is obscured)");
        FlushOverlay(pScreen);
        mali->bOverlayWinEnabled = FALSE;
        sunxi_layer_hide(disp);
        return;
    }

    /* If the window got moved -> update overlay position */
    if (!mali->bOverlayWinOverlapped &&
        (mali->overlay_x != mali->pOverlayWin->drawable.x ||
         mali->overlay_y != mali->pOverlayWin->drawable.y))
    {
        mali->overlay_x = mali->pOverlayWin->drawable.x;
        mali->overlay_y = mali->pOverlayWin->drawable.y;

        sunxi_layer_set_output_window(disp, mali->pOverlayWin->drawable.x,
                                      mali->pOverlayWin->drawable.y,
                                      mali->pOverlayWin->drawable.width,
                                      mali->pOverlayWin->drawable.height);
        DEBUG_MSG(2,"Move overlay to (%d, %d)", mali->overlay_x, mali->overlay_y);
    }

    /* If the window got unobscured -> enable overlay */
    if (!mali->bOverlayWinOverlapped && !mali->bOverlayWinEnabled) {
        DEBUG_MSG(2,"Enabling overlay (window is fully unobscured)");
        mali->bOverlayWinEnabled = TRUE;
        sunxi_layer_show(disp);
    }
}

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    Bool ret;
    DrawablePtr pDraw = &pWin->drawable;
    DRI2WindowStatePtr window_state = FBTurboGetWindowDriverPrivate(pDraw);

    if (window_state) {
        DEBUG_MSG(2,"Free DRI2 bookkeeping for window %p", pWin);
        FBTurboDelWindowDriverPrivate(pDraw, window_state);
        if (window_state->bo_mem_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_mem_ptr);
        if (window_state->bo_back_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_back_ptr);
        if (window_state->bo_front_ptr)
            unref_bo_info(mali->bo_ops, window_state->bo_front_ptr);
        free(window_state);
    }

    if (pWin == mali->pOverlayWin) {
        sunxi_disp_t *disp = SUNXI_DISP(pScrn);
        sunxi_layer_hide(disp);
        mali->pOverlayWin = NULL;
        DEBUG_MSG(2,"DestroyWindow %p", pWin);
    }

    pScreen->DestroyWindow = mali->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWin);
    mali->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = DestroyWindow;

    return ret;
}

static void
PostValidateTree(WindowPtr pWin, WindowPtr pLayerWin, VTKind kind)
{
    ScreenPtr pScreen = pWin ? pWin->drawable.pScreen : pLayerWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);

    if (mali->PostValidateTree) {
        pScreen->PostValidateTree = mali->PostValidateTree;
        (*pScreen->PostValidateTree) (pWin, pLayerWin, kind);
        mali->PostValidateTree = pScreen->PostValidateTree;
        pScreen->PostValidateTree = PostValidateTree;
    }

    UpdateOverlay(pScreen);
}

/*
 * If somebody is trying to make a screenshot, we want to have DRI2 buffer
 * flushed.
 */
static void
GetImage(DrawablePtr pDrawable, int x, int y, int w, int h,
         unsigned int format, unsigned long planeMask, char *d)
{
    ScreenPtr pScreen = pDrawable->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);

    /* FIXME: more precise check */
    if (mali->pOverlayDirtyBO)
        FlushOverlay(pScreen);

    if (mali->GetImage) {
        pScreen->GetImage = mali->GetImage;
        (*pScreen->GetImage) (pDrawable, x, y, w, h, format, planeMask, d);
        mali->GetImage = pScreen->GetImage;
        pScreen->GetImage = GetImage;
    }
}

static Bool
DestroyPixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    Bool result;
    BOInfoPtr bo;

    bo = FBTurboGetPixmapDriverPrivate(pPixmap);

    if (bo) {
        DEBUG_MSG(2,"DestroyPixmap %p for migrated BO pixmap (BO=%p)", pPixmap, bo);

        pPixmap->devKind = bo->BackupDevKind;
        pPixmap->devPrivate.ptr = bo->BackupDevPrivatePtr;

        FBTurboDelPixmapDriverPrivate(pPixmap, bo);

        if (bo->pPixmap == pPixmap)
            bo->pPixmap = NULL;
        if (bo->handle != fPtr->scanout)
            unref_bo_info(mali->bo_ops, bo);
    }

    pScreen->DestroyPixmap = mali->DestroyPixmap;
    result = (*pScreen->DestroyPixmap) (pPixmap);
    mali->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = DestroyPixmap;


    return result;
}

#if 0
static Bool
ModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
                   int depth, int bitsPerPixel, int devKind,
                   void *pPixData)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    Bool result;

    //TRACE_ENTER();

    result = FBTurboModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData);

    if (!result) {
        pScreen->ModifyPixmapHeader = mali->ModifyPixmapHeader;
        result = (*pScreen->ModifyPixmapHeader) (pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData);
        mali->ModifyPixmapHeader = pScreen->ModifyPixmapHeader;
        pScreen->ModifyPixmapHeader = ModifyPixmapHeader;
    }

    //TRACE_EXIT();

    return result;
}
#endif

static void EnableHWCursor(ScrnInfoPtr pScrn)
{
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

    if (!mali->bHardwareCursorIsInUse) {
        DEBUG_MSG(2,"EnableHWCursor");
        mali->bHardwareCursorIsInUse = TRUE;
    }

    UpdateOverlay(screenInfo.screens[pScrn->scrnIndex]);

    if (mali->EnableHWCursor) {
        hwc->EnableHWCursor = mali->EnableHWCursor;
        (*hwc->EnableHWCursor) (pScrn);
        mali->EnableHWCursor = hwc->EnableHWCursor;
        hwc->EnableHWCursor = EnableHWCursor;
    }
}

static void DisableHWCursor(ScrnInfoPtr pScrn)
{
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

    if (mali->bHardwareCursorIsInUse) {
        mali->bHardwareCursorIsInUse = FALSE;
        DEBUG_MSG(2,"DisableHWCursor");
    }

    UpdateOverlay(screenInfo.screens[pScrn->scrnIndex]);

    if (mali->DisableHWCursor) {
        hwc->DisableHWCursor = mali->DisableHWCursor;
        (*hwc->DisableHWCursor) (pScrn);
        mali->DisableHWCursor = hwc->DisableHWCursor;
        hwc->DisableHWCursor = DisableHWCursor;
    }
}

static const char *driverNames[1] = {
    "lima" /* DRI2DriverDRI */
};

static const char *driverNamesWithVDPAU[2] = {
    "lima", /* DRI2DriverDRI */
    "sunxi" /* DRI2DriverVDPAU */
};

static char *
fbturbo_dri2_probe_driver_name(ScreenPtr pScreen, DRI2InfoPtr info)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    drmVersionPtr version = drmGetVersion(info->fd);
    char *kernel_driver;

    if (!version) {
        ERROR_MSG(
                   "[DRI2] Couldn't drmGetVersion() on non-PCI device, "
                   "no driver name found.");
        return NULL;
    }

    kernel_driver = strndup(version->name, version->name_len);
    drmFreeVersion(version);
    return kernel_driver;
}

static Bool
fbturbo_dri2_init_driver_name(ScreenPtr pScreen, DRI2InfoPtr info)
{
    if (info->version == 3 || info->numDrivers == 0) {
        const char **ds_driverNames;

        /* Driver too old: use the old-style driverName field */
        info->numDrivers = info->driverName ? 1 : 2;
        ds_driverNames = xallocarray(info->numDrivers, sizeof(*ds_driverNames));
        if (!ds_driverNames)
            return FALSE;

        if (info->driverName) {
            ds_driverNames[0] = info->driverName;
        } else {
            ds_driverNames[0] = ds_driverNames[1] = fbturbo_dri2_probe_driver_name(pScreen, info);
            if (!ds_driverNames[0])
                return FALSE;
        }

        info->driverNames = ds_driverNames;
    }

    return TRUE;
}

FBTurboMaliDRI2 *FBTurboMaliDRI2_Init(ScreenPtr pScreen,
                                  Bool      bUseOverlay,
                                  Bool      bSwapbuffersWait,
                                  Bool      bUseDumb)
{
    DRI2InfoRec info = { 0 };
    FBTurboMaliDRI2 *mali;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);

    if (fPtr->drmFD < 0) {
        ERROR_MSG("FBTurboMaliDRI2_Init: drmOpen failed!");
        return NULL;
    }

    if (!(mali = calloc(1, sizeof(FBTurboMaliDRI2)))) {
        ERROR_MSG("FBTurboMaliDRI2_Init: calloc failed");
        return NULL;
    }

    mali->bo_dev = fPtr->bo_dev;
    mali->bo_ops = fPtr->bo_ops;

    if (!mali->bo_ops) {
        drmClose(fPtr->drmFD);
        fPtr->drmFD = -1;
        free(mali);
        ERROR_MSG("FBTurboMaliDRI2_Init: mali->bo_ops is not set");
        return NULL;
    }

    mali->ump_alternative_fb_secure_id = FBTURBO_BO_INVALID_SECURE_ID;

    if (disp && bUseOverlay) {
        /* Try to get UMP framebuffer wrapper with secure id 1 */
        if (!bUseDumb)
            ioctl(disp->fd_fb, GET_UMP_SECURE_ID_BUF1, &mali->ump_alternative_fb_secure_id);

        /* Try to allocate a small dummy BO to secure id 2 */
        mali->bo_null_handle1 = mali->bo_ops->new(mali->bo_dev, 32, 32, 0, 32, 0,
                                               FBTURBO_BO_TYPE_NONE);
        if (mali->bo_ops->valid(mali->bo_null_handle1))
            mali->bo_null_secure_id = mali->bo_ops->secure_id_get(mali->bo_null_handle1);

        mali->bo_null_handle2 = FBTURBO_BO_INVALID_HANDLE;

        /* Try to get UMP framebuffer for the secure id other than 1 and 2 */
        if (!bUseDumb) {
            if (ioctl(disp->fd_fb, GET_UMP_SECURE_ID_SUNXI_FB, &mali->ump_fb_secure_id) ||
                                       mali->ump_fb_secure_id == FBTURBO_BO_INVALID_SECURE_ID) {
                INFO_MSG(
                      "GET_UMP_SECURE_ID_SUNXI_FB ioctl failed, overlays can't be used");
                mali->ump_fb_secure_id = FBTURBO_BO_INVALID_SECURE_ID;
            }
        }
        else
            mali->ump_fb_secure_id = FBTURBO_BO_INVALID_SECURE_ID;

        if (mali->ump_alternative_fb_secure_id == FBTURBO_BO_INVALID_SECURE_ID ||
            mali->bo_ops->get_size_from_secure_id(mali->ump_alternative_fb_secure_id) !=
                                          disp->framebuffer_size) {
            INFO_MSG(
                  "UMP does not wrap the whole framebuffer, overlays can't be used");
            mali->ump_fb_secure_id = FBTURBO_BO_INVALID_SECURE_ID;
            mali->ump_alternative_fb_secure_id = FBTURBO_BO_INVALID_SECURE_ID;
        }

        if (disp->framebuffer_size - disp->gfx_layer_size <
                                                 disp->xres * disp->yres * 4 * 2) {
            int needed_fb_num = (disp->xres * disp->yres * 4 * 2 +
                                 disp->gfx_layer_size - 1) / disp->gfx_layer_size + 1;
            INFO_MSG(
                "tear-free zero-copy double buffering needs more video memory");
            INFO_MSG(
                "please set fb0_framebuffer_num >= %d in the fex file", needed_fb_num);
            INFO_MSG(
                "and sunxi_fb_mem_reserve >= %d in the kernel cmdline",
                (needed_fb_num * disp->gfx_layer_size + 1024 * 1024 - 1) / (1024 * 1024));
        }
    }
    else {
        /* Try to allocate small dummy BOs to secure id 1 and 2 */
        mali->bo_null_handle1 = mali->bo_ops->new(mali->bo_dev, 32, 32, 0, 32, 0, 
                                               FBTURBO_BO_TYPE_NONE);
        if (mali->bo_ops->valid(mali->bo_null_handle1))
            mali->bo_null_secure_id = mali->bo_ops->secure_id_get(mali->bo_null_handle1);

        mali->bo_null_handle2 = mali->bo_ops->new(mali->bo_dev, 32, 32, 0, 32, 0, 
                                               FBTURBO_BO_TYPE_NONE);

        /* No UMP wrappers for the framebuffer are available */
        mali->ump_fb_secure_id = FBTURBO_BO_INVALID_SECURE_ID;
    }

    if (mali->bo_null_secure_id > 2) {
        INFO_MSG(
                   "warning, can't workaround Mali r3p0 window resize bug");
    }

    if (disp && mali->ump_fb_secure_id != FBTURBO_BO_INVALID_SECURE_ID)
        INFO_MSG(
              "enabled display controller hardware overlays for DRI2");
    else if (bUseOverlay)
        INFO_MSG(
              "display controller hardware overlays can't be used for DRI2");
    else
        INFO_MSG(
              "display controller hardware overlays are not used for DRI2");

    INFO_MSG( "Wait on SwapBuffers? %s",
               bSwapbuffersWait ? "enabled" : "disabled");

#if DRI2INFOREC_VERSION < 9
    info.version = FBTURBO_DRI2INFOREC_VER;
#else
    info.version = 9;
#endif

    INFO_MSG("dri2 version set: %d", info.version);

    if (fPtr->drmType > 0) {
        info.driverName = NULL; /* Compat field, unused. */
        /* These two will be filled in by dri2.c */
        info.numDrivers = 0;
        info.driverNames = NULL;
    }
    else if (fPtr->have_sunxi_cedar) {
        info.numDrivers = ARRAY_SIZE(driverNamesWithVDPAU);
        info.driverName = driverNamesWithVDPAU[0];
        info.driverNames = driverNamesWithVDPAU;
    }
    else {
        info.numDrivers = ARRAY_SIZE(driverNames);
        info.driverName = driverNames[0];
        info.driverNames = driverNames;
    }
    info.deviceName = drmGetDeviceNameFromFd(fPtr->drmFD);
    info.fd = fPtr->drmFD;

    info.CreateBuffer = MaliDRI2CreateBuffer;
    info.DestroyBuffer = MaliDRI2DestroyBuffer;
    info.CopyRegion = MaliDRI2CopyRegion;

    info.CreateBuffer2 = MaliDRI2CreateBuffer2;
    info.DestroyBuffer2 = MaliDRI2DestroyBuffer2;
    info.CopyRegion2 = MaliDRI2CopyRegion2;

    /* driverNames will be filled in by dri2.c since xorg-server 1.17.x
       maybe this part can be surrounded by 
       if ABI_VIDEODRV_VERSION < SET_ABI_VERSION(19, 0) */
    fbturbo_dri2_init_driver_name(pScreen, &info);

    if (!DRI2ScreenInit(pScreen, &info)) {
        drmClose(fPtr->drmFD);
        fPtr->drmFD = -1;
        free(mali);
        return NULL;
    }
    else {
        SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

        /* Wrap the current DestroyWindow function */
        mali->DestroyWindow = pScreen->DestroyWindow;
        pScreen->DestroyWindow = DestroyWindow;

	if (!fPtr->UseEXA) {
            /* Wrap the current PostValidateTree function */
            mali->PostValidateTree = pScreen->PostValidateTree;
            pScreen->PostValidateTree = PostValidateTree;
            /* Wrap the current GetImage function */
            mali->GetImage = pScreen->GetImage;
            pScreen->GetImage = GetImage;
            /* Wrap the current DestroyPixmap function */
            mali->DestroyPixmap = pScreen->DestroyPixmap;
            pScreen->DestroyPixmap = DestroyPixmap;
#if 0
            /* Wrap the current ModifyPixmapHeader function */
            mali->ModifyPixmapHeader = pScreen->ModifyPixmapHeader;
            pScreen->ModifyPixmapHeader = ModifyPixmapHeader;
#endif
	}

        /* Wrap hardware cursor callback functions */
        if (hwc) {
            mali->EnableHWCursor = hwc->EnableHWCursor;
            hwc->EnableHWCursor = EnableHWCursor;
            mali->DisableHWCursor = hwc->DisableHWCursor;
            hwc->DisableHWCursor = DisableHWCursor;
        }

        mali->drm_fd = fPtr->drmFD;
        mali->bSwapbuffersWait = bSwapbuffersWait;
        return mali;
    }
}

void FBTurboMaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
    SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

    /* Unwrap functions */
    pScreen->DestroyWindow    = mali->DestroyWindow;

    if (!fPtr->UseEXA) {
        pScreen->PostValidateTree = mali->PostValidateTree;
        pScreen->GetImage         = mali->GetImage;
        pScreen->DestroyPixmap    = mali->DestroyPixmap;
#if 0
        pScreen->ModifyPixmapHeader = mali->ModifyPixmapHeader;
#endif
    }

    if (hwc) {
        hwc->EnableHWCursor  = mali->EnableHWCursor;
        hwc->DisableHWCursor = mali->DisableHWCursor;
    }

    if (mali->bo_ops->valid(mali->bo_null_handle1))
        mali->bo_ops->release(mali->bo_null_handle1);
    if (mali->bo_ops->valid(mali->bo_null_handle2))
        mali->bo_ops->release(mali->bo_null_handle2);

    drmClose(mali->drm_fd);
    fPtr->drmFD = -1;
    DRI2CloseScreen(pScreen);
}
