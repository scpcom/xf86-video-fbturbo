#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>

#include "xorgVersion.h"
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "sunxi_disp_hwcursor.h"
#include "sunxi_mali_ump_dri2.h"

#include "exa.h"

DevPrivateKeyRec FBTurboScreenPrivateKeyRec;

#ifndef USE_DIX_PRIVATE
unsigned long
FBTurboGetPixmapPitch(PixmapPtr pPixmap)
{
	return pPixmap->devKind;
}

void *
FBTurboGetPixmapDriverPrivate(PixmapPtr pPixmap)
{
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr bo = NULL;

	HASH_FIND_PTR(mali->HashPixmapToBO, &pPixmap, bo);

	if (!bo)
		bo = exaGetPixmapDriverPrivate(pPixmap);

	return bo;
}

void
FBTurboSetPixmapDriverPrivate(PixmapPtr pPixmap, void* ptr)
{
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr bo = ptr;

	HASH_ADD_PTR(mali->HashPixmapToBO, pPixmap, bo);
}

void
FBTurboDelPixmapDriverPrivate(PixmapPtr pPixmap, void* ptr)
{
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	BOInfoPtr bo = ptr;

	HASH_DEL(mali->HashPixmapToBO, bo);
}

void *FBTurboGetWindowDriverPrivate(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	DRI2WindowStatePtr window_state = NULL;

#ifdef DEBUG
	if (!pDraw)
		INFO_MSG("%s: pDraw is null", __func__);
	else if (pDraw->type == UNDRAWABLE_WINDOW)
		INFO_MSG("%s: pDraw is a undrawable window", __func__);
	else if (pDraw->type == DRAWABLE_WINDOW)
		INFO_MSG("%s: pDraw is a window", __func__);
	else if (pDraw->type == DRAWABLE_PIXMAP)
		INFO_MSG("%s: pDraw is a pixmap", __func__);
	else
		INFO_MSG("%s: pDraw is unknown", __func__);
#endif

	HASH_FIND_PTR(mali->HashWindowState, &pDraw, window_state);

	return window_state;
}

void FBTurboSetWindowDriverPrivate(DrawablePtr pDraw, void* ptr)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	DRI2WindowStatePtr window_state = ptr;

#ifdef DEBUG
	if (pDraw->type == UNDRAWABLE_WINDOW)
		INFO_MSG("%s: pDraw is a undrawable window", __func__);
#endif

	HASH_ADD_PTR(mali->HashWindowState, pDraw, window_state);
}

void FBTurboDelWindowDriverPrivate(DrawablePtr pDraw, void* ptr)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBTurboMaliDRI2 *mali = FBTURBO_MALI_DRI2(pScrn);
	DRI2WindowStatePtr window_state = ptr;

#ifdef DEBUG
	if (pDraw->type == UNDRAWABLE_WINDOW)
		INFO_MSG("%s: pDraw is a undrawable window", __func__);
#endif

	HASH_DEL(mali->HashWindowState, window_state);
}

#else
unsigned long
FBTurboGetPixmapPitch(PixmapPtr pPixmap)
{
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->UseEXA)
		return exaGetPixmapPitch(pPixmap);
	else
		return pPixmap->devKind;
}

void *
FBTurboGetPixmapDriverPrivate(PixmapPtr pPixmap)
{
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	void* ptr = NULL;

	if (fPtr->UseEXA)
                ptr = exaGetPixmapDriverPrivate(pPixmap);
	else {
		FBTurboPixmapPriv(pPixmap);
		if (pFBTurboPixmap)
			ptr = pFBTurboPixmap->driverPriv;
	}

	return ptr;
}

void
FBTurboSetPixmapDriverPrivate(PixmapPtr pPixmap, void* ptr)
{
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (!fPtr->UseEXA) {
		FBTurboPixmapPriv(pPixmap);

		if (pFBTurboPixmap)
			pFBTurboPixmap->driverPriv = ptr;
	}
}

void
FBTurboDelPixmapDriverPrivate(PixmapPtr pPixmap, void* ptr)
{
	FBTurboSetPixmapDriverPrivate(pPixmap, NULL);
}

static void *
_FBTurboGetWindowDriverPrivate(DrawablePtr pDraw)
{
	WindowPtr pWindow = (WindowPtr)pDraw;
	FBTurboWindowPriv(pWindow);

	if (pFBTurboWindow)
		return pFBTurboWindow->driverPriv;
	else
		return NULL;
}

void *
FBTurboGetWindowDriverPrivate(DrawablePtr pDraw)
{
	if (pDraw->type == UNDRAWABLE_WINDOW)
		return NULL;

	//if (!draw2pix(pDraw))
	//	return NULL;

	return _FBTurboGetWindowDriverPrivate(pDraw);
}

static void
_FBTurboSetWindowDriverPrivate(DrawablePtr pDraw, void* ptr)
{
	WindowPtr pWindow = (WindowPtr)pDraw;
	FBTurboWindowPriv(pWindow);

	if (pFBTurboWindow)
		pFBTurboWindow->driverPriv = ptr;
}

void
FBTurboSetWindowDriverPrivate(DrawablePtr pDraw, void* ptr)
{
	if (pDraw->type == UNDRAWABLE_WINDOW)
		return;

	//if (!draw2pix(pDraw))
	//	return;

	_FBTurboSetWindowDriverPrivate(pDraw, ptr);
}

void
FBTurboDelWindowDriverPrivate(DrawablePtr pDraw, void* ptr)
{
	FBTurboSetWindowDriverPrivate(pDraw, NULL);
}
#endif

Bool
InitFBTurboPriv(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
#ifdef USE_DIX_PRIVATE
    FBTurboDixScreenPrivPtr pFBTurboScreen;

    INFO_MSG("Using dix privates");

    if (!dixRegisterPrivateKey(&FBTurboScreenPrivateKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    pFBTurboScreen = calloc(sizeof(FBTurboDixScreenPrivRec), 1);
    if (!pFBTurboScreen) {
        WARNING_MSG("Failed to allocate screen private");
        return FALSE;
    }

    dixSetPrivate(&pScreen->devPrivates, FBTurboScreenPrivateKey, pFBTurboScreen);

    if (!dixRegisterScreenSpecificPrivateKey
        (pScreen, &pFBTurboScreen->windowPrivateKeyRec, PRIVATE_WINDOW,
         sizeof(FBTurboDixWindowPrivRec))) {
        WARNING_MSG("Failed to allocate window private");
        return FALSE;
    }

    if (!dixRegisterScreenSpecificPrivateKey
        (pScreen, &pFBTurboScreen->pixmapPrivateKeyRec, PRIVATE_PIXMAP,
         sizeof(FBTurboDixPixmapPrivRec))) {
        WARNING_MSG("Failed to allocate pixmap private");
        return FALSE;
    }

#else
    INFO_MSG("Using ut_hash privates");
#endif

    return TRUE;
}

