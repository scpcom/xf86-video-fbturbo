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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* all driver need this */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "mipointer.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"
#include "dgaproc.h"
#include "xf86drm.h"
#include "xf86Crtc.h"
#include "xf86drmMode.h"
#include "xf86RandR12.h"

#include "cpu_backend.h"
#include "fb_copyarea.h"

#include "sunxi_disp.h"
#include "sunxi_disp_hwcursor.h"
#include "sunxi_x_g2d.h"
#include "backing_store_tuner.h"
#include "sunxi_video.h"

#include "fbdev_lcd.h"

#ifdef HAVE_LIBBCM_HOST
#include "fbdev_vc4.h"
#endif

#include "sunxi_mali_ump_dri2.h"

/* for visuals */
#include "fb.h"

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif

#include "fbdevhw.h"

#include "xf86xv.h"

#include "compat-api.h"

#ifdef HAVE_LIBBCM_HOST
#if ABI_VIDEODRV_VERSION >= SET_ABI_VERSION(24, 0)
#ifdef XSERVER_PLATFORM_BUS
#define FBTURBO_XSERVER_PLATFORM_BUS
#endif
#endif
#endif

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
#include "xf86platformBus.h"
#endif
#ifdef XSERVER_LIBPCIACCESS
#include <pciaccess.h>
#endif

static Bool debug = 0;

#if 0
#define TRACE_ENTER(str) \
    do { if (debug) ERROR_STR("fbturbo: " str " %d",pScrn->scrnIndex); } while (0)
#define TRACE_EXIT(str) \
    do { if (debug) ERROR_STR("fbturbo: " str " done"); } while (0)
#endif
#define TRACE(str) \
    do { if (debug) ERROR_STR("fbturbo trace: " str ""); } while (0)

/* -------------------------------------------------------------------- */
/* prototypes                                                           */

static const OptionInfoRec * FBDevAvailableOptions(int chipid, int busid);
static void	FBDevIdentify(int flags);
static Bool	FBDevProbe(DriverPtr drv, int flags);
#ifdef XSERVER_LIBPCIACCESS
static Bool	FBDevPciProbe(DriverPtr drv, int entity_num,
     struct pci_device *dev, intptr_t match_data);
#endif
static Bool	FBDevPreInit(ScrnInfoPtr pScrn, int flags);
static Bool	FBDevScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool	FBDevCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static void	FBDevFreeScreen(FREE_SCREEN_ARGS_DECL);
static void	FBDevBlockHandler(BLOCKHANDLER_ARGS_DECL);
static void	FBDevBlockHandler_oneshot(ScreenPtr pScreen, void *pTimeout);
static void *	FBDevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
				  CARD32 *size, void *closure);
static void	FBDevPointerMoved(SCRN_ARG_TYPE arg, int x, int y);
static Bool	FBDevDGAInit(ScrnInfoPtr pScrn, ScreenPtr pScreen);
static Bool	FBDevDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
				pointer ptr);

#define FBTurboHWLoadPalette fbdevHWLoadPaletteWeak()

enum { FBDEV_ROTATE_NONE=0, FBDEV_ROTATE_CW=270, FBDEV_ROTATE_UD=180, FBDEV_ROTATE_CCW=90 };


/* -------------------------------------------------------------------- */

/*
 * This is intentionally screen-independent.  It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

#define DRM_DEVICE "/dev/dri/card%d"

#define FBDEV_VERSION		4000
#define FBDEV_NAME		"FBTURBO"
#define FBDEV_DRIVER_NAME	"fbturbo"
#define FBDEV_CHIP_NAME         "fbturbo"

#ifdef XSERVER_LIBPCIACCESS
static const struct pci_id_match fbdev_device_match[] = {
    {
	PCI_MATCH_ANY, PCI_MATCH_ANY, PCI_MATCH_ANY, PCI_MATCH_ANY,
	0x00030000, 0x00ffffff, 0
    },

    { 0, 0, 0 },
};
#endif

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
static Bool
fbdev_platform_probe(DriverPtr driver,
                  int entity_num, int flags, struct xf86_platform_device *dev,
                  intptr_t match_data);
#endif

_X_EXPORT DriverRec FBDEV = {
	FBDEV_VERSION,
	FBDEV_DRIVER_NAME,
#if 0
	"driver for linux framebuffer devices",
#endif
	FBDevIdentify,
	FBDevProbe,
	FBDevAvailableOptions,
	NULL,
	0,
	FBDevDriverFunc,

#ifdef XSERVER_LIBPCIACCESS
    fbdev_device_match,
    FBDevPciProbe,
#endif
#ifdef FBTURBO_XSERVER_PLATFORM_BUS
    fbdev_platform_probe,
#endif
};

/* Supported "chipsets" */
static SymTabRec FBDevChipsets[] = {
    { 0, FBDEV_CHIP_NAME },
    {-1, NULL }
};

/* Supported options */
typedef enum {
	OPTION_SHADOW_FB,
	OPTION_ROTATE,
	OPTION_FBDEV,
	OPTION_DEBUG,
	OPTION_HW_CURSOR,
	OPTION_SW_CURSOR,
	OPTION_DRI2,
	OPTION_DRI2_OVERLAY,
	OPTION_SWAPBUFFERS_WAIT,
	OPTION_ACCELMETHOD,
	OPTION_USE_BS,
	OPTION_FORCE_BS,
	OPTION_XV_OVERLAY,
	OPTION_USE_DUMB,
} FBDevOpts;

static const OptionInfoRec FBDevOptions[] = {
	{ OPTION_SHADOW_FB,	"ShadowFB",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_ROTATE,	"Rotate",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_FBDEV,		"fbdev",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_DEBUG,		"debug",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_HW_CURSOR,	"HWCursor",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_SW_CURSOR,	"SWCursor",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_DRI2,		"DRI2",		OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_DRI2_OVERLAY,	"DRI2HWOverlay",OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_SWAPBUFFERS_WAIT,"SwapbuffersWait",OPTV_BOOLEAN,{0},	FALSE },
	{ OPTION_ACCELMETHOD,	"AccelMethod",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_USE_BS,	"UseBackingStore",OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_FORCE_BS,	"ForceBackingStore",OPTV_BOOLEAN,{0},	FALSE },
	{ OPTION_XV_OVERLAY,	"XVHWOverlay",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_USE_DUMB,      "UseDumb",	OPTV_BOOLEAN,   {0},    FALSE },
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

/* -------------------------------------------------------------------- */

#ifdef XFree86LOADER

int fbdev_entity_index = -1;

MODULESETUPPROTO(FBDevSetup);

static XF86ModuleVersionInfo FBDevVersRec =
{
	"fbturbo",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData fbturboModuleData = { &FBDevVersRec, FBDevSetup, NULL };

pointer
FBDevSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&FBDEV, module, HaveDriverFuncs);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

#endif /* XFree86LOADER */

/* -------------------------------------------------------------------- */
/* our private data, and two functions to allocate/free this            */

#include "fbdev_priv.h"

#define MAXBUFSIZE 16384

static const char *textinfo_match_prefix(const char *s, const char *prefix)
{
    const char *result;
    if (strncmp(s, prefix, strlen(prefix)) != 0)
        return NULL;
    result = s;
    if (!result)
        return NULL;
    result += strlen(prefix);
    while (*result && (*result == ' ' || *result == '\t'))
        result++;
    return result;
}

static int parse_text_info(const char *filename, const char *prefix, const char *fmt, int *out)
{
    char *buffer = (char *)malloc(MAXBUFSIZE);
    FILE *fd;
    const char *val;

    if (!buffer)
        return 0;

    DEBUG_STR(2, "Trying to open %s", filename);
    fd = fopen(filename, "r");
    if (!fd) {
        free(buffer);
        return 0;
    }

    DEBUG_STR(2, "Reading %s", filename);
    while (fgets(buffer, MAXBUFSIZE, fd)) {
        if (!strchr(buffer, '\n') && !feof(fd)) {
            fclose(fd);
            free(buffer);
            return 0;
        }
        if ((val = textinfo_match_prefix(buffer, prefix))) {
            int sret;
            DEBUG_STR(2, "match prefix %s, val %s", prefix, val);
            sret = sscanf(val, fmt, out);
            DEBUG_STR(2, "sscanf(val, '%s', out) = %i", fmt, sret);
            if (sret == 1) {
                fclose(fd);
                free(buffer);
                return 1;
            }
        }
    }
    fclose(fd);
    free(buffer);
    return 0;
}

static int fb_hw_init(ScrnInfoPtr pScrn, const char *device)
{
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    /* use /dev/fb0 by default */
    if (!device)
        device = "/dev/fb0";

    fPtr->fb_lcd_fd = open(device, O_RDWR);
    if (fPtr->fb_lcd_fd < 0) {
        ERROR_STR("%s failed to open %s", __func__, device);
        close(fPtr->fb_lcd_fd);
        return 0;
    }

    if (ioctl(fPtr->fb_lcd_fd, FBIOGET_FSCREENINFO, &fPtr->fb_lcd_fix))
    {
        ERROR_STR("%s FBIOGET_FSCREENINFO failed for %s!", __func__, device);
        return 0;
    }

    if (ioctl(fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, &fPtr->fb_lcd_var))
    {
        ERROR_STR("%s FBIOGET_VSCREENINFO failed for %s!", __func__, device);
        return 0;
    }


    return 1;
}

static Bool fbdev_crtc_config_resize(ScrnInfoPtr pScrn, int width, int height)
{
	INFO_MSG("%s: width = %d height = %d", __FUNCTION__, width, height);

	return TRUE;
}

static const xf86CrtcConfigFuncsRec fbdev_crtc_config_funcs =
{
	.resize = fbdev_crtc_config_resize,
#if ABI_VIDEODRV_VERSION >= SET_ABI_VERSION(24, 0)
	.create_lease = fbdev_create_lease,
	.terminate_lease = fbdev_terminate_lease,
#endif
};

static void FBDev_crtc_config(ScrnInfoPtr pScrn)
{
	int max_width, max_height;
	TRACE_ENTER();

	/* Allocate an xf86CrtcConfig */
	xf86CrtcConfigInit(pScrn, &fbdev_crtc_config_funcs);

	max_width = 4096;
	max_height = 4096;

	xf86CrtcSetSizeRange(pScrn, 640, 480, max_width, max_height);
	TRACE_EXIT();
}

#if USE_CRTC_AND_LCD
static Bool FBTurboHWSetMode(ScrnInfoPtr pScrn, DisplayModePtr mode, Bool check)
{
        FBTurboHWPtr fPtr = FBTURBOHWPTR(pScrn);

        TRACE_ENTER();

        IGNORE(fPtr);
        IGNORE(mode);
        IGNORE(check);

        return TRUE;
}

static Bool FBTurboHWModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
        FBTurboHWPtr fPtr = FBTURBOHWPTR(pScrn);

        TRACE_ENTER();

        pScrn->vtSema = TRUE;

        if (!FBTurboHWSetMode(pScrn, mode, FALSE))
        {
                return FALSE;
        }

        if (0 != ioctl(fPtr->fb_lcd_fd, FBIOGET_FSCREENINFO, (void *)(&fPtr->fb_lcd_fix)))
        {
                ERROR_MSG("FBIOGET_FSCREENINFO: %s", strerror(errno));
                return FALSE;
        }

        if (0 != ioctl(fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, (void *)(&fPtr->fb_lcd_var)))
        {
                ERROR_MSG("FBIOGET_VSCREENINFO: %s", strerror(errno));
                return FALSE;
        }

        if (pScrn->defaultVisual == TrueColor || pScrn->defaultVisual == DirectColor)
        {
                pScrn->offset.red   = fPtr->fb_lcd_var.red.offset;
                pScrn->offset.green = fPtr->fb_lcd_var.green.offset;
                pScrn->offset.blue  = fPtr->fb_lcd_var.blue.offset;
                pScrn->mask.red     = ((1 << fPtr->fb_lcd_var.red.length) - 1) << fPtr->fb_lcd_var.red.offset;
                pScrn->mask.green   = ((1 << fPtr->fb_lcd_var.green.length) - 1) << fPtr->fb_lcd_var.green.offset;
                pScrn->mask.blue    = ((1 << fPtr->fb_lcd_var.blue.length) - 1) << fPtr->fb_lcd_var.blue.offset;
        }

        return TRUE;
}
#else
#define FBTurboHWModeInit fbdevHWModeInit
#endif

static void FBTurboFBSetVideoModes(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	TRACE_ENTER();

	if (NULL == pScrn->modes)
	{
#ifdef HAVE_LIBBCM_HOST
		int v_w = 0;
		int v_h = 0;
#endif
		int val = 0;
		float vrefresh = 60.0;
		uint16_t interlaced = 0;

		DisplayModePtr fbdev_mode = NULL;

		if (fPtr->isFBDevHW)
			fbdev_mode = fbdevHWGetBuildinMode(pScrn);

		if (fbdev_mode)
			val = fbdev_lcd_vrefresh(fbdev_mode->VRefresh, fbdev_mode->Clock, fbdev_mode->HTotal, fbdev_mode->VTotal, fbdev_mode->VScan, fbdev_mode->Flags);

		if (val > 0)
		{
			vrefresh = val;
			INFO_MSG("Got rate %.0f from fbdevHW vrefresh", vrefresh);
			val = 0;
		}
		else if (parse_text_info("/sys/devices/platform/meson-fb/graphics/fb0/flush_rate", "flush_rate:", "[%i]", &val))
			INFO_MSG("Got rate %i from platform/meson-fb", val);
		else if (parse_text_info("/sys/class/video/frame_rate", "VF.fps=", "%*f panel fps %i,", &val))
			INFO_MSG("Got rate %i from class/video", val);
#ifdef HAVE_LIBBCM_HOST
		else if (vc_vchi_tv_get_status((uint32_t *)&v_w, (uint32_t *)&v_h, &vrefresh, &interlaced))
		{
			INFO_MSG("Got rate %.0f from vchi_tv", vrefresh);
			val = 0;
		}
#endif
		else
			val = 0;

		if (val)
			vrefresh = val;

		INFO_MSG("Adding current mode: %i x %i %.0f", fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh);
		DEBUG_MSG(1, "pclock %i lm %i rm %i um %i lm %i hl %i vl %i sy %i vm %i",
	          fPtr->fb_lcd_var.pixclock,                 /* pixel clock in ps (pico seconds) */
	          fPtr->fb_lcd_var.left_margin,              /* time from sync to picture    */
	          fPtr->fb_lcd_var.right_margin,             /* time from picture to sync    */
	          fPtr->fb_lcd_var.upper_margin,             /* time from sync to picture    */
	          fPtr->fb_lcd_var.lower_margin,
	          fPtr->fb_lcd_var.hsync_len,                /* length of horizontal sync    */
	          fPtr->fb_lcd_var.vsync_len,                /* length of vertical sync      */
	          fPtr->fb_lcd_var.sync,                     /* see FB_SYNC_*                */
	          fPtr->fb_lcd_var.vmode);                    /* see FB_VMODE_*               */

		if (fbdev_mode && fbdev_mode->HDisplay && fbdev_mode->VDisplay)
		{
			fbdev_copy_mode(fbdev_mode, &fPtr->buildin);

			if (!fPtr->buildin.VRefresh && fPtr->buildin.Clock)
			{
				vrefresh = 1000.0 * ((float) fPtr->buildin.Clock) / ((float) fPtr->buildin.VTotal) / ((float) fPtr->buildin.HTotal);
				fPtr->buildin.VRefresh = vrefresh;
				INFO_MSG("Got rate %.0f from fbdevHW pixelclock/vtotal/htotal", vrefresh);
			}

			if (!fPtr->buildin.HSync && fPtr->buildin.Clock)
			{
				fPtr->buildin.HSync = ((float) fPtr->buildin.Clock) / ((float) fPtr->buildin.HTotal);
			}

			FBTurboFBListVideoMode(pScrn, &fPtr->buildin, "Buildin mode:");

			if (!fPtr->buildin.VRefresh)
			{
				INFO_MSG("buildin has no VRefresh, using xf86CVTMode to create a mode");
				fbdev_mode = xf86CVTMode(fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh, FALSE, interlaced);
				if (fbdev_mode->Clock > 151550)
					fbdev_mode = xf86CVTMode(fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh, TRUE, interlaced);
				fbdev_mode->VRefresh = vrefresh;
				fbdev_copy_mode(fbdev_mode, &fPtr->buildin);
				fPtr->buildin.type |= M_T_BUILTIN;
				fbdev_fill_crtc_mode(&fPtr->buildin, fbdev_mode->HDisplay, fbdev_mode->VDisplay, fbdev_mode->VRefresh, M_T_BUILTIN, NULL);
			}
			xf86SetModeDefaultName(&fPtr->buildin);
		}
		else
		{
			INFO_MSG("creating internal mode based on vrefresh %.0f", vrefresh);
			fbdev_fill_mode(&fPtr->buildin, fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh, M_T_BUILTIN, NULL);
		}

		pScrn->modes = fbdev_make_mode(fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh, M_T_BUILTIN, NULL);
		fbdev_copy_mode(&fPtr->buildin, pScrn->modes);
		FBTurboFBListVideoMode(pScrn, pScrn->modes, "New buildin mode:");
	}
}


void FBTurboFBListVideoMode(ScrnInfoPtr pScrn, DisplayModePtr mode, const char *msg)
{
	if (mode)
	{
			int xres = mode->HDisplay;
			int yres = mode->VDisplay;
			int pclk = mode->Clock;
			float vref = mode->VRefresh;

			DEBUG_MSG(1, "%s %i x %i %.0f %i", msg, xres, yres, vref, pclk);

			if (!vref)
			{
				float vrev;

				vref = 60.0;
				pclk = (int)(vref * mode->VTotal * mode->HTotal / 1000.0);
				vrev = 1000.0 * pclk / mode->VTotal / mode->HTotal;
				DEBUG_MSG(1, "new vref %.2f pclk %i vrev %.2f", vref, pclk, vrev);
			}

			DEBUG_MSG(1, "t %i, name %s, hs %.1f vr %.1f",
				mode->type,
				mode->name,
				mode->HSync,
				mode->VRefresh);

			DEBUG_MSG(1, "sees hd %i hss %i hse %i ht %i, hsk %i, vd %i vss %i vse %i vt %i, c %i",
				mode->HDisplay,
				mode->HSyncStart,
				mode->HSyncEnd,
				mode->HTotal,
				mode->HSkew,
				mode->VDisplay,
				mode->VSyncStart,
				mode->VSyncEnd,
				mode->VTotal,
				mode->Clock);

			DEBUG_MSG(1, "sees vs %i f 0x%x (%i)",
				mode->VScan,
				mode->Flags, mode->Flags);

			DEBUG_MSG(1, "crtc hd %i hss %i hse %i ht %i, hsk %i, vd %i vss %i vse %i vt %i, c %i",
				mode->CrtcHDisplay,
				mode->CrtcHSyncStart,
				mode->CrtcHSyncEnd,
				mode->CrtcHTotal,
				mode->CrtcHSkew,
				mode->CrtcVDisplay,
				mode->CrtcVSyncStart,
				mode->CrtcVSyncEnd,
				mode->CrtcVTotal,
				mode->ClockIndex);

			DEBUG_MSG(1, "crtc hbs %i hbe %i, vbs %i vbe %i",
				mode->CrtcHBlankStart,
				mode->CrtcHBlankEnd,
				mode->CrtcVBlankStart,
				mode->CrtcVBlankEnd);
	}
}

static void FBTurboFBListVideoModes(ScrnInfoPtr pScrn, DisplayModePtr modes)
{
        TRACE_ENTER();

        if (modes)
        {
                DisplayModePtr mode, first = mode = modes;

                do
                {
			FBTurboFBListVideoMode(pScrn, mode, "Having mode:");
 
			mode = mode->next;
		}
		while (mode != NULL && mode != first);
	}

	TRACE_EXIT();
}

#ifndef FBTurboHWUseBuildinMode
static void FBTurboHWUseBuildinMode(ScrnInfoPtr pScrn)
{
	FBTurboHWPtr fPtr = FBTURBOHWPTR(pScrn);

	TRACE_ENTER();

	FBTurboFBSetVideoModes(pScrn);

        pScrn->modes    = &fPtr->buildin;
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;

        if (pScrn->virtualX < fPtr->buildin.HDisplay)
        {
                pScrn->virtualX = fPtr->buildin.HDisplay;
        }

        if (pScrn->virtualY < fPtr->buildin.VDisplay)
        {
                pScrn->virtualY = fPtr->buildin.VDisplay;
        }

	TRACE_EXIT();
}
#endif

/**
 * Helper functions for sharing a DRM connection across screens.
 */
static struct FBTurboConnection {
	const char *driver_name;
	const char *bus_id;
	unsigned int card_num;
	int fd;
	int open_count;
	int master_count;
} connection = {NULL, NULL, 0, -1, 0, 0};

static int
FBTurboSetDRMMaster(ScrnInfoPtr pScrn)
{
#ifdef XF86_PDEV_SERVER_FD
	FBDevPtr fPtr = FBDEVPTR(pScrn);
#endif
	int ret = 0;

#ifdef XF86_PDEV_SERVER_FD
	if (fPtr->pEnt->location.type == BUS_PLATFORM &&
	    (fPtr->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
		return ret;
#endif

	if (connection.fd < 0)
		return ret;

	if (!connection.master_count)
		ret = drmSetMaster(connection.fd);

	if (!ret)
		connection.master_count++;

	return ret;
}

static int
FBTurboDropDRMMaster(void)
{
	int ret = 0;

	if (connection.fd < 0)
		return ret;

	assert(connection.master_count > 0);

	if (1 == connection.master_count)
		ret = drmDropMaster(connection.fd);

	if (!ret)
		connection.master_count--;

	return ret;
}

FBDevEntPtr fbdev_ent_priv(ScrnInfoPtr pScrn);

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
static int open_hw(const char *dev);
#endif

static Bool
FBTurboOpenDRMCard(ScrnInfoPtr pScrn)
{
    int drm_fd = -1;
    int drm_type = 0;
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    FBDevEntPtr fEntPtr = fbdev_ent_priv(pScrn);
#ifdef FBTURBO_XSERVER_PLATFORM_BUS
    EntityInfoPtr pEnt = fPtr->pEnt;
#endif
    Bool have_sunxi_cedar = TRUE;

    if (!xf86LoadKernelModule("mali"))
        INFO_MSG("can't load 'mali' kernel module");
    if (!xf86LoadKernelModule("mali_drm"))
        INFO_MSG("can't load 'mali_drm' kernel module");

    if (!xf86LoadKernelModule("sunxi_cedar_mod")) {
        INFO_MSG("can't load 'sunxi_cedar_mod' kernel module");
        have_sunxi_cedar = FALSE;
    }

    fPtr->drmFD = -1;
    fPtr->drmType = -1;
    fPtr->have_sunxi_cedar = have_sunxi_cedar;

    if (!xf86LoadSubModule(pScrn, "dri2"))
        return FALSE;

    if (fEntPtr->fd) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   " reusing fd for second head\n");
        fPtr->drmFD = fEntPtr->fd;
	fPtr->drmType = 3;
        fEntPtr->fd_ref++;
        return TRUE;
    }

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
    if (pEnt->location.type == BUS_PLATFORM) {
        drm_type = 3;
#ifdef XF86_PDEV_SERVER_FD
        if (pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD)
            drm_fd =
                xf86_platform_device_odev_attributes(pEnt->location.id.plat)->
                fd;
        else
#endif
        {
            char *path =
                xf86_platform_device_odev_attributes(pEnt->location.id.plat)->
                path;
            drm_fd = open_hw(path);
        }
    }
#endif

    if (drm_fd < 0) {
        drm_type = 0;
        drm_fd = drmOpen("mali_drm", NULL);
    }

    if (drm_fd < 0) {
        drm_type = 1;
        drm_fd = drmOpen("sun4i-drm", NULL);
    }

    if (drm_fd < 0) {
        drm_type = 2;
        drm_fd = drmOpen("meson", NULL);
    }

    if (drm_fd < 0) {
        drm_type = 3;
        drm_fd = drmOpen("vc4", NULL);
    }

    if (drm_fd < 0) {
	const char *dev;
	char filename[32];

	drm_type = 3;

        dev = getenv("KMSDEVICE");
        if ((NULL == dev) || ((drm_fd = open(dev, O_RDWR | O_CLOEXEC, 0)) == -1)) {

		/* open with card_num */
		snprintf(filename, sizeof(filename),
				DRM_DEVICE, connection.card_num);
		INFO_STR(
				"No BusID or DriverName specified - opening %s",
				filename);
		drm_fd = open(filename, O_RDWR, 0);
	}
    }

    if (drm_fd < 0) {
        ERROR_STR("%s: drmOpen failed!", __func__);
        return FALSE;
    }

    fPtr->drmFD = drm_fd;
    fPtr->drmType = drm_type;

    fEntPtr->fd = fPtr->drmFD;
    fEntPtr->fd_ref = 1;

    return TRUE;
}

static Bool
FBTurboOpenDRM(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	drmSetVersion sv;
	int err;

	if (connection.fd < 0) {
		assert(!connection.open_count);
		assert(!connection.master_count);
		FBTurboOpenDRMCard(pScrn);
		if (fPtr->drmFD < 0)
			return FALSE;
		/* Check that what we are or can become drm master by
		 * attempting a drmSetInterfaceVersion(). If successful
		 * this leaves us as master.
		 * (see DRIOpenDRMMaster() in DRI1)
		 */
		sv.drm_di_major = 1;
		sv.drm_di_minor = 1;
		sv.drm_dd_major = -1;
		sv.drm_dd_minor = -1;
		err = drmSetInterfaceVersion(fPtr->drmFD, &sv);
		if (err != 0) {
			ERROR_MSG("Cannot set the DRM interface version.");
			drmClose(fPtr->drmFD);
			fPtr->drmFD = -1;
			return FALSE;
		}
		connection.fd = fPtr->drmFD;
		connection.open_count = 1;
		connection.master_count = 1;
	} else {
		assert(connection.open_count);
		connection.open_count++;
		connection.master_count++;
		fPtr->drmFD = connection.fd;
	}

	return TRUE;
}

/**
 * The driver's SwitchMode() function.  Initialize the new mode for the
 * Screen.
 */
static Bool
FBTurboSwitchMode(SWITCH_MODE_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}



/**
 * The driver's AdjustFrame() function.  For cases where the frame buffer is
 * larger than the monitor resolution, this function can pan around the frame
 * buffer within the "viewport" of the monitor.
 */
static void
FBTurboAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
	IGNORE(arg);
}



/**
 * The driver's EnterVT() function.  This is called at server startup time, and
 * when the X server takes over the virtual terminal from the console.  As
 * such, it may need to save the current (i.e. console) HW state, and set the
 * HW state as needed by the X server.
 */
static Bool
FBTurboEnterVT(VT_FUNC_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
#if 0
#else
	FBDevPtr fPtr = FBDEVPTR(pScrn);
#endif
	int i, ret;

	TRACE_ENTER();

	for (i = 1; i < currentMaxClients; i++) {
		if (clients[i] && !clients[i]->clientGone)
			AttendClient(clients[i]);
	}

	ret = FBTurboSetDRMMaster(pScrn);
	if (ret) {
		ERROR_MSG("Cannot get DRM master: %s", strerror(errno));
		return FALSE;
	}

#if 0
	if (!xf86SetDesiredModes(pScrn)) {
#else
	if (!fbdev_set_desired_modes(pScrn, fPtr, TRUE)) {
#endif
		ERROR_MSG("xf86SetDesiredModes() failed!");
		return FALSE;
	}

	TRACE_EXIT();
	return TRUE;
}



/**
 * The driver's LeaveVT() function.  This is called when the X server
 * temporarily gives up the virtual terminal to the console.  As such, it may
 * need to restore the console's HW state.
 */
static void
FBTurboLeaveVT(VT_FUNC_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	int i, ret;

	TRACE_ENTER();

	for (i = 1; i < currentMaxClients; i++) {
		if (clients[i] && !clients[i]->clientGone)
			IgnoreClient(clients[i]);
	}

	ret = FBTurboDropDRMMaster();
	if (ret)
		WARNING_MSG("drmDropMaster failed: %s", strerror(errno));

	TRACE_EXIT();
}

static Bool
FBDevGetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;
	
	pScrn->driverPrivate = xnfcalloc(sizeof(FBDevRec), 1);
	return TRUE;
}

static void
FBDevFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/* -------------------------------------------------------------------- */

static const OptionInfoRec *
FBDevAvailableOptions(int chipid, int busid)
{
	return FBDevOptions;
}

static void
FBDevIdentify(int flags)
{
	xf86PrintChipsets(FBDEV_NAME, "driver for framebuffer", FBDevChipsets);
}

FBDevEntPtr fbdev_ent_priv(ScrnInfoPtr pScrn)
{
    DevUnion     *pPriv;
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    pPriv = xf86GetEntityPrivate(fPtr->pEnt->index,
                                 fbdev_entity_index);
    return pPriv->ptr;
}

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
static void
fbdev_setup_scrn_hooks(ScrnInfoPtr pScrn)
{
	pScrn->driverVersion = FBDEV_VERSION;
	pScrn->driverName    = FBDEV_DRIVER_NAME;
	pScrn->name          = FBDEV_NAME;

	pScrn->Probe         = NULL;
	pScrn->PreInit       = FBDevPreInit;
	pScrn->ScreenInit    = FBDevScreenInit;
	pScrn->FreeScreen    = FBDevFreeScreen;
#if 0
	pScrn->SwitchMode    = fbdevHWSwitchModeWeak();
	pScrn->AdjustFrame   = fbdevHWAdjustFrameWeak();
	pScrn->EnterVT       = fbdevHWEnterVTWeak();
	pScrn->LeaveVT       = fbdevHWLeaveVTWeak();
	pScrn->ValidMode     = fbdevHWValidModeWeak();
#else
	pScrn->SwitchMode    = FBTurboSwitchMode;
	pScrn->AdjustFrame   = FBTurboAdjustFrame;
	pScrn->EnterVT       = FBTurboEnterVT;
	pScrn->LeaveVT       = FBTurboLeaveVT;
#endif
}
#endif

static void
fbdev_setup_entity(ScrnInfoPtr pScrn, int entity_num)
{
    DevUnion *pPriv;

    xf86SetEntitySharable(entity_num);

    if (fbdev_entity_index == -1)
        fbdev_entity_index = xf86AllocateEntityPrivateIndex();

    pPriv = xf86GetEntityPrivate(entity_num,
                                 fbdev_entity_index);

    xf86SetEntityInstanceForScreen(pScrn, entity_num, xf86GetNumEntityInstances(entity_num) - 1);

    if (!pPriv->ptr)
        pPriv->ptr = xnfcalloc(sizeof(FBDevEntRec), 1);
}

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
static int
get_passed_fd(void)
{
#if 0
    if (xf86DRMMasterFd >= 0) {
        xf86DrvMsg(-1, X_INFO, "Using passed DRM master file descriptor %d\n", xf86DRMMasterFd);
        return dup(xf86DRMMasterFd);
    }
#endif
    return -1;
}

static int
open_hw(const char *dev)
{
    int fd;

    if ((fd = get_passed_fd()) != -1)
        return fd;

    if (dev)
        fd = open(dev, O_RDWR | O_CLOEXEC, 0);
    else {
        dev = getenv("KMSDEVICE");
        if ((NULL == dev) || ((fd = open(dev, O_RDWR | O_CLOEXEC, 0)) == -1)) {
            dev = "/dev/dri/card0";
            fd = open(dev, O_RDWR | O_CLOEXEC, 0);
        }
    }
    if (fd == -1)
        xf86DrvMsg(-1, X_ERROR, "open %s: %s\n", dev, strerror(errno));

    return fd;
}

static int
check_outputs(int fd, int *count)
{
    drmModeResPtr res = drmModeGetResources(fd);
    int ret;

    if (!res)
        return FALSE;

    if (count)
        *count = res->count_connectors;

    ret = res->count_connectors > 0;
    drmModeFreeResources(res);
    return ret;
}

static Bool
probe_hw(const char *dev, struct xf86_platform_device *platform_dev)
{
    int fd;

#ifdef XF86_PDEV_SERVER_FD
    if (platform_dev && (platform_dev->flags & XF86_PDEV_SERVER_FD)) {
        fd = xf86_platform_device_odev_attributes(platform_dev)->fd;
        if (fd == -1)
            return FALSE;
        return check_outputs(fd, NULL);
    }
#endif

    fd = open_hw(dev);
    if (fd != -1) {
        int ret = check_outputs(fd, NULL);

        close(fd);
        return ret;
    }
    return FALSE;
}
#endif

#ifdef XSERVER_LIBPCIACCESS
static Bool FBDevPciProbe(DriverPtr drv, int entity_num,
			  struct pci_device *dev, intptr_t match_data)
{
    ScrnInfoPtr pScrn = NULL;

    if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
	return FALSE;
	    
    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, NULL, NULL,
				NULL, NULL, NULL, NULL);
    if (pScrn) {
	const char *device;
	GDevPtr devSection = xf86GetDevFromEntity(pScrn->entityList[0],
						  pScrn->entityInstanceList[0]);

	device = xf86FindOptionValue(devSection->options, "fbdev");
	if (fbdevHWProbe(NULL, (char*)device, NULL)) {
	    pScrn->driverVersion = FBDEV_VERSION;
	    pScrn->driverName    = FBDEV_DRIVER_NAME;
	    pScrn->name          = FBDEV_NAME;
	    pScrn->Probe         = FBDevProbe;
	    pScrn->PreInit       = FBDevPreInit;
	    pScrn->ScreenInit    = FBDevScreenInit;
	    pScrn->FreeScreen    = FBDevFreeScreen;
#if 0
	    pScrn->SwitchMode    = fbdevHWSwitchModeWeak();
	    pScrn->AdjustFrame   = fbdevHWAdjustFrameWeak();
	    pScrn->EnterVT       = fbdevHWEnterVTWeak();
	    pScrn->LeaveVT       = fbdevHWLeaveVTWeak();
	    pScrn->ValidMode     = fbdevHWValidModeWeak();
#else
	    pScrn->SwitchMode    = FBTurboSwitchMode;
	    pScrn->AdjustFrame   = FBTurboAdjustFrame;
	    pScrn->EnterVT       = FBTurboEnterVT;
	    pScrn->LeaveVT       = FBTurboLeaveVT;
#endif

	    CONFIG_MSG(
		       "claimed PCI slot %d@%d:%d:%d", 
		       dev->bus, dev->domain, dev->dev, dev->func);
	    INFO_MSG(
		       "using %s", device ? device : "default device");
	}
	else {
	    pScrn = NULL;
	}
    }

    return (pScrn != NULL);
}
#endif

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
static Bool
fbdev_platform_probe(DriverPtr driver,
                  int entity_num, int flags, struct xf86_platform_device *dev,
                  intptr_t match_data)
{
    ScrnInfoPtr pScrn = NULL;
    const char *path = xf86_platform_device_odev_attributes(dev)->path;
    int scr_flags = 0;

    TRACE("platform probe start");

    if (flags & PLATFORM_PROBE_GPU_SCREEN)
        scr_flags = XF86_ALLOCATE_GPU_SCREEN;

    if (probe_hw(path, dev)) {
        pScrn = xf86AllocateScreen(driver, scr_flags);
        if (xf86IsEntitySharable(entity_num))
            xf86SetEntityShared(entity_num);
        xf86AddEntityToScreen(pScrn, entity_num);

        fbdev_setup_scrn_hooks(pScrn);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "using drv %s\n", path ? path : "default device");

        fbdev_setup_entity(pScrn, entity_num);
    }

    TRACE("platform probe end");

    return pScrn != NULL;
}
#endif

static Bool
FBDevProbe(DriverPtr drv, int flags)
{
	int i;
	ScrnInfoPtr pScrn;
       	GDevPtr *devSections;
	int numDevSections;
#ifndef XSERVER_LIBPCIACCESS
	int bus,device,func;
#endif
	const char *dev;
	Bool foundScreen = FALSE;

	TRACE("probe start");

	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

	if ((numDevSections = xf86MatchDevice(FBDEV_DRIVER_NAME, &devSections)) <= 0) 
	    return FALSE;
	
	if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
	    return FALSE;
	    
	for (i = 0; i < numDevSections; i++) {
	    Bool isIsa = FALSE;
	    Bool isPci = FALSE;

	    dev = xf86FindOptionValue(devSections[i]->options,"fbdev");
	    if (devSections[i]->busID) {
#ifndef XSERVER_LIBPCIACCESS
	        if (xf86ParsePciBusString(devSections[i]->busID,&bus,&device,
					  &func)) {
		    if (!xf86CheckPciSlot(bus,device,func))
		        continue;
		    isPci = TRUE;
		} else
#endif
#ifdef HAVE_ISA
		if (xf86ParseIsaBusString(devSections[i]->busID))
		    isIsa = TRUE;
		else
#endif
		{
		    /* no-op */
		}
		  
	    }
	    if (fbdevHWProbe(NULL,(char*)dev,NULL)) {
		int entity;
		pScrn = NULL;
		if (isPci) {
#ifndef XSERVER_LIBPCIACCESS
		    /* XXX what about when there's no busID set? */
		    entity = xf86ClaimPciSlot(bus,device,func,drv,
					      0,devSections[i],
					      TRUE);
		    pScrn = xf86ConfigPciEntity(pScrn,0,entity,
						      NULL,RES_SHARED_VGA,
						      NULL,NULL,NULL,NULL);
		    /* xf86DrvMsg() can't be called without setting these */
		    pScrn->driverName    = FBDEV_DRIVER_NAME;
		    pScrn->name          = FBDEV_NAME;
		    CONFIG_MSG(
			       "claimed PCI slot %d:%d:%d",bus,device,func);

#endif
		} else if (isIsa) {
#ifdef HAVE_ISA
		    entity = xf86ClaimIsaSlot(drv, 0,
					      devSections[i], TRUE);
		    pScrn = xf86ConfigIsaEntity(pScrn,0,entity,
						      NULL,RES_SHARED_VGA,
						      NULL,NULL,NULL,NULL);
#endif
		} else {
		    entity = xf86ClaimFbSlot(drv, 0,
					      devSections[i], TRUE);
		    pScrn = xf86ConfigFbEntity(pScrn,0,entity,
					       NULL,NULL,NULL,NULL);
		   
		}
		if (pScrn) {
		    foundScreen = TRUE;
		    
		    pScrn->driverVersion = FBDEV_VERSION;
		    pScrn->driverName    = FBDEV_DRIVER_NAME;
		    pScrn->name          = FBDEV_NAME;
		    pScrn->Probe         = FBDevProbe;
		    pScrn->PreInit       = FBDevPreInit;
		    pScrn->ScreenInit    = FBDevScreenInit;
		    pScrn->FreeScreen    = FBDevFreeScreen;
#if 0
		    pScrn->SwitchMode    = fbdevHWSwitchModeWeak();
		    pScrn->AdjustFrame   = fbdevHWAdjustFrameWeak();
		    pScrn->EnterVT       = fbdevHWEnterVTWeak();
		    pScrn->LeaveVT       = fbdevHWLeaveVTWeak();
		    pScrn->ValidMode     = fbdevHWValidModeWeak();
#else
		    pScrn->SwitchMode    = FBTurboSwitchMode;
		    pScrn->AdjustFrame   = FBTurboAdjustFrame;
		    pScrn->EnterVT       = FBTurboEnterVT;
		    pScrn->LeaveVT       = FBTurboLeaveVT;
#endif

		    INFO_MSG(
			       "using %s", dev ? dev : "default device");

		    fbdev_setup_entity(pScrn, entity);
		}
	    }
	}
	free(devSections);
	TRACE("probe done");
	return foundScreen;
}

static int
FBTurboHWGetType(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWGetType(pScrn);
	else
		return FBDEVHW_PACKED_PIXELS;
}

static int
FBTurboHWGetLineLength(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWGetLineLength(pScrn);
	else
		return pScrn->virtualX * (pScrn->bitsPerPixel / 8);
}

static int
FBTurboHWGetVidmem(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWGetVidmem(pScrn);
	else {
		int ret = FBTurboHWGetLineLength(pScrn) * pScrn->virtualY;
		if (!ret)
			ret = fPtr->fb_lcd_var.xres * (pScrn->bitsPerPixel / 8) * fPtr->fb_lcd_var.yres;
		return ret;
	}
}

static void *
FBTurboHWMapVidmem(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWMapVidmem(pScrn);
	else
		return NULL;
}

static Bool
FBTurboHWUnmapVidmem(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWUnmapVidmem(pScrn);
	else
		return TRUE;
}

static int
FBTurboHWLinearOffset(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWLinearOffset(pScrn);
	else
		return 0;
}

static int
FBTurboHWGetDepth(ScrnInfoPtr pScrn, int *fbbpp)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWGetDepth(pScrn, fbbpp);
	else {
		int depth = 0;
		fbdev_get_default_bpp(pScrn, fPtr, &depth, fbbpp);
		return depth;
	}
}

static char *
FBTurboHWGetName(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->isFBDevHW)
		return fbdevHWGetName(pScrn);
	else
		return (char *)FBDEV_CHIP_NAME;
}

static Bool
FBDevPreInit(ScrnInfoPtr pScrn, int flags)
{
	FBDevPtr fPtr;
	int default_depth, fbbpp;
	const char *device;
	const char *s;
	int type;
	cpuinfo_t *cpuinfo;

	if (flags & PROBE_DETECT) return FALSE;

	TRACE_ENTER("PreInit");

	/* Check the number of entities, and fail if it isn't one. */
	if (pScrn->numEntities != 1)
		return FALSE;

	pScrn->monitor = pScrn->confScreen->monitor;

	FBDevGetRec(pScrn);
	fPtr = FBDEVPTR(pScrn);

	fPtr->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

	fPtr->isFBDevHW = TRUE;

#ifndef XSERVER_LIBPCIACCESS
	pScrn->racMemFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;
	/* XXX Is this right?  Can probably remove RAC_FB */
	pScrn->racIoFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;

	if (fPtr->pEnt->location.type == BUS_PCI &&
	    xf86RegisterResources(fPtr->pEnt->index,NULL,ResExclusive)) {
		ERROR_MSG(
		   "xf86RegisterResources() found resource conflicts");
		return FALSE;
	}
#endif

#ifdef FBTURBO_XSERVER_PLATFORM_BUS
	if (FBTurboOpenDRM(pScrn)) {
		fPtr->isFBDevHW = FALSE;
	} else {
#endif
		INFO_MSG("find device");
		device = xf86FindOptionValue(fPtr->pEnt->device->options,"fbdev");
		INFO_MSG("open device %s", device);
		/* open device */
		if (!fbdevHWInit(pScrn,NULL,(char*)device)) {
			INFO_MSG("Could not open device %s", device);
			return FALSE;
		}
#ifdef FBTURBO_XSERVER_PLATFORM_BUS
	}
#endif

	default_depth = FBTurboHWGetDepth(pScrn,&fbbpp);

	INFO_MSG( "xf86SetDepthBpp(pScrn, %d, %d, %d, ...)", default_depth, default_depth, fbbpp);
	if (!xf86SetDepthBpp(pScrn, default_depth, default_depth, fbbpp,
			     Support24bppFb | Support32bppFb | SupportConvert32to24 | SupportConvert24to32))
		return FALSE;
	xf86PrintDepthBpp(pScrn);

	/* Get the depth24 pixmap format */
	if (pScrn->depth == 24 && pix24bpp == 0)
		pix24bpp = xf86GetBppFromDepth(pScrn, 24);

	/* color weight */
	if (pScrn->depth > 8) {
		rgb zeros = { 0, 0, 0 };
		if (!xf86SetWeight(pScrn, zeros, zeros))
			return FALSE;
	}

	/* visual init */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;

	/* We don't currently support DirectColor at > 8bpp */
	if (pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
		ERROR_MSG( "requested default visual"
			   " (%s) is not supported at depth %d",
			   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
		return FALSE;
	}

	{
		Gamma zeros = {0.0, 0.0, 0.0};

		if (!xf86SetGamma(pScrn,zeros)) {
			return FALSE;
		}
	}

	pScrn->progClock = TRUE;
	pScrn->rgbBits   = 8;
	pScrn->chipset   = FBDEV_CHIP_NAME;

	/* handle options */
	xf86CollectOptions(pScrn, NULL);
	if (!(fPtr->Options = malloc(sizeof(FBDevOptions))))
		return FALSE;
	memcpy(fPtr->Options, FBDevOptions, sizeof(FBDevOptions));
	xf86ProcessOptions(pScrn->scrnIndex, fPtr->pEnt->device->options, fPtr->Options);

#ifdef HAVE_LIBUMP
	fPtr->UseDumb = xf86ReturnOptValBool(fPtr->Options, OPTION_USE_DUMB, FALSE);
#else
	fPtr->UseDumb = TRUE;
#endif

        if (!FBTurboOpenDRM(pScrn)) {
                ERROR_MSG("FBTurboOpenDRM failed");
                fPtr->UseDumb = FALSE;
        }

	/* follow the DRI2 and UseDumb option */
	fPtr->UseEXA = xf86ReturnOptValBool(fPtr->Options, OPTION_DRI2, TRUE) && fPtr->UseDumb;

	/* check the processor type */
	cpuinfo = cpuinfo_init();
	INFO_MSG( "processor: %s",
	           cpuinfo->processor_name);
	/* don't use shadow by default if we have VFP/NEON or HW acceleration */
	fPtr->shadowFB = !cpuinfo->has_arm_vfp && !fPtr->UseDumb && !fPtr->UseEXA &&
	                 !xf86GetOptValString(fPtr->Options, OPTION_ACCELMETHOD);
	cpuinfo_close(cpuinfo);

	/* but still honour the settings from xorg.conf */
	fPtr->shadowFB = xf86ReturnOptValBool(fPtr->Options, OPTION_SHADOW_FB,
					      fPtr->shadowFB);

	debug = xf86ReturnOptValBool(fPtr->Options, OPTION_DEBUG, FALSE);

	fPtr->NoFlip = FALSE;

	/* rotation */
	fPtr->rotate = FBDEV_ROTATE_NONE;
	if ((s = xf86GetOptValString(fPtr->Options, OPTION_ROTATE)))
	{
	  if(!xf86NameCmp(s, "CW"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_CW;
	    CONFIG_MSG(
		       "rotating screen clockwise");
	  }
	  else if(!xf86NameCmp(s, "CCW"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_CCW;
	    CONFIG_MSG(
		       "rotating screen counter-clockwise");
	  }
	  else if(!xf86NameCmp(s, "UD"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_UD;
	    CONFIG_MSG(
		       "rotating screen upside-down");
	  }
	  else
	  {
	    CONFIG_MSG(
		       "\"%s\" is not a valid value for Option \"Rotate\"", s);
	    INFO_MSG(
		       "valid options are \"CW\", \"CCW\" and \"UD\"");
	  }
	}

	if (!fb_hw_init(pScrn, xf86FindOptionValue(fPtr->pEnt->device->options,"fbdev")))
	{
		ERROR_MSG("fb_hw_init failed");
		return FALSE;
	}

	pScrn->videoRam  = FBTurboHWGetVidmem(pScrn);

	INFO_MSG( "hardware: %s (video memory:"
		   " %dkB)", FBTurboHWGetName(pScrn), pScrn->videoRam/1024);

#if USE_CRTC_AND_LCD
	pScrn->frameX0 = 0;
	pScrn->frameY0 = 0;
	pScrn->frameX1 = fPtr->fb_lcd_var.xres;
	pScrn->frameY1 = fPtr->fb_lcd_var.yres;

	DEBUG_MSG(1, "FBDev_crtc_config");
	FBDev_crtc_config(pScrn);

	FBTurboFBSetVideoModes(pScrn);

	fPtr->bo_ops = NULL;

	if (fPtr->UseDumb)
		fPtr->bo_ops = &dumb_bo_ops;
#ifdef HAVE_LIBUMP
	else
		fPtr->bo_ops = &ump_bo_ops;
#endif

	if (fPtr->bo_ops) {
		if (!fPtr->bo_ops->open(&fPtr->bo_dev, fPtr->drmFD)) {
			ERROR_STR("%s: fPtr->bo_ops->open() failed", __func__);
			fPtr->bo_ops = NULL;
		}
	}

	DEBUG_MSG(1, "FBDEV_lcd_init");
	if (!FBDEV_lcd_init(pScrn))
	{
		ERROR_MSG("FBDev_lcd_init failed!");
		return FALSE;
	}

	DEBUG_MSG(1, "xf86ProviderSetup");
	xf86ProviderSetup(pScrn, NULL, "fbturbo");

	DEBUG_MSG(1, "xf86InitialConfiguration");
	if (!xf86InitialConfiguration(pScrn, TRUE))
	{
		ERROR_MSG("xf86InitialConfiguration failed!");
		return FALSE;
	}

	xf86RandR12PreInit(pScrn);
#endif

	/* select video modes */

	if (fPtr->isFBDevHW) {
		INFO_MSG("checking modes against framebuffer device...");
		FBTurboHWSetVideoModes(pScrn);
	}

	INFO_MSG("checking modes against monitor...");
	{
		DisplayModePtr mode, first = mode = pScrn->modes;
		
		if (mode != NULL) do {
			mode->status = xf86CheckModeForMonitor(mode, pScrn->monitor);
			mode = mode->next;
		} while (mode != NULL && mode != first);

		xf86PruneDriverModes(pScrn);
	}

	if (NULL == pScrn->modes)
	{
		DEBUG_MSG(1, "FBTurboHWUseBuildinMode");
		FBTurboHWUseBuildinMode(pScrn);
	}
	pScrn->currentMode = pScrn->modes;

	/* First approximation, may be refined in ScreenInit */
	pScrn->displayWidth = pScrn->virtualX;

	FBTurboFBListVideoModes(pScrn, pScrn->modes);

	xf86PrintModes(pScrn);

	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);

	/* Load bpp-specific modules */
	switch ((type = FBTurboHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel)
		{
		case 8:
		case 16:
		case 24:
		case 32:
			break;
		default:
			ERROR_MSG(
			"unsupported number of bits per pixel: %d",
			pScrn->bitsPerPixel);
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
               /* Not supported yet, don't know what to do with this */
               ERROR_MSG(
                          "interleaved planes are not yet supported by the "
			  "fbdev driver");
		return FALSE;
	case FBDEVHW_TEXT:
               /* This should never happen ...
                * we should check for this much much earlier ... */
               ERROR_MSG(
                          "text mode is not supported by the fbdev driver");
		return FALSE;
       case FBDEVHW_VGA_PLANES:
               /* Not supported yet */
               ERROR_MSG(
                          "EGA/VGA planes are not yet supported by the fbdev "
			  "driver");
               return FALSE;
       default:
               ERROR_MSG(
                          "unrecognised fbdev hardware type (%d)", type);
               return FALSE;
	}

	if (xf86LoadSubModule(pScrn, "fb") == NULL) {
		FBDevFreeRec(pScrn);
		return FALSE;
	}

	/* Load shadow if needed */
	if (fPtr->shadowFB) {
		CONFIG_MSG( "using shadow"
			   " framebuffer");
		if (!xf86LoadSubModule(pScrn, "shadow")) {
			FBDevFreeRec(pScrn);
			return FALSE;
		}
	}

	TRACE_EXIT("PreInit");
	return TRUE;
}


#if ABI_VIDEODRV_VERSION >= SET_ABI_VERSION(24, 0)
static void
FBDevUpdatePacked(ScreenPtr pScreen, shadowBufPtr pBuf)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->rotate)
		shadowUpdateRotatePacked(pScreen, pBuf);
	else
		shadowUpdatePacked(pScreen, pBuf);

}
#endif

static Bool
FBDevCreateScreenResources(ScreenPtr pScreen)
{
    PixmapPtr pPixmap;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    Bool ret;
    void *pixels = NULL;

    pScreen->CreateScreenResources = fPtr->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = FBDevCreateScreenResources;

    if (!fbdev_set_desired_modes(pScrn, fPtr, pScrn->is_gpu)) {
        TRACE_EXIT();
        return FALSE;
    }

    if (!ret)
	return FALSE;

    pPixmap = pScreen->GetScreenPixmap(pScreen);

    if (!pScreen->ModifyPixmapHeader(pPixmap, -1, -1, -1, -1, -1, pixels))
        FatalError("Couldn't adjust screen pixmap\n");

    if(fPtr->shadowFB) {
#if ABI_VIDEODRV_VERSION >= SET_ABI_VERSION(24, 0)
	if (!shadowAdd(pScreen, pPixmap, FBDevUpdatePacked,
#else
	if (!shadowAdd(pScreen, pPixmap, fPtr->rotate ?
		   shadowUpdateRotatePackedWeak() : shadowUpdatePackedWeak(),
#endif
		   FBDevWindowLinear, fPtr->rotate, NULL)) {
		return FALSE;
	}
    }

    return TRUE;
}

static Bool
FBDevShadowInit(ScreenPtr pScreen)
{
    if (!shadowSetup(pScreen)) {
	return FALSE;
    }

    return TRUE;
}

static void
fbdev_load_palette(ScrnInfoPtr pScrn, int numColors,
                     int *indices, LOCO * colors, VisualPtr pVisual)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    uint16_t lut_r[256], lut_g[256], lut_b[256];
    int index, j, i;
    int c;

    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        //struct fbdev_lcd_crtc_private_rec *fbdev_crtc = crtc->driver_private;

        for (i = 0; i < 256; i++) {
            lut_r[i] = 0; /* fbdev_crtc->lut_r[i] << 6; */
            lut_g[i] = 0; /* fbdev_crtc->lut_g[i] << 6; */
            lut_b[i] = 0; /* fbdev_crtc->lut_b[i] << 6; */
        }

        switch (pScrn->depth) {
        case 15:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                for (j = 0; j < 8; j++) {
                    lut_r[index * 8 + j] = colors[index].red << 6;
                    lut_g[index * 8 + j] = colors[index].green << 6;
                    lut_b[index * 8 + j] = colors[index].blue << 6;
                }
            }
            break;
        case 16:
            for (i = 0; i < numColors; i++) {
                index = indices[i];

                if (i <= 31) {
                    for (j = 0; j < 8; j++) {
                        lut_r[index * 8 + j] = colors[index].red << 6;
                        lut_b[index * 8 + j] = colors[index].blue << 6;
                    }
                }

                for (j = 0; j < 4; j++) {
                    lut_g[index * 4 + j] = colors[index].green << 6;
                }
            }
            break;
        default:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                lut_r[index] = colors[index].red << 6;
                lut_g[index] = colors[index].green << 6;
                lut_b[index] = colors[index].blue << 6;
            }
            break;
        }

        /* Make the change through RandR */
        if (crtc->randr_crtc)
            RRCrtcGammaSet(crtc->randr_crtc, lut_r, lut_g, lut_b);
        else
            crtc->funcs->gamma_set(crtc, lut_r, lut_g, lut_b, 256);
    }
}

static Bool
FBDevScreenInit(SCREEN_INIT_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	VisualPtr visual;
	xf86CrtcConfigPtr xf86_config;
	int j;
	int init_picture = 0;
	int ret, flags;
	int type;
	int depth;
	const char *accelmethod;
	cpu_backend_t *cpu_backend;
	Bool useBackingStore = FALSE, forceBackingStore = FALSE;

	TRACE_ENTER("FBDevScreenInit");

	/* set drm master before allocating scanout buffer */
	if (FBTurboSetDRMMaster(pScrn)) {
		ERROR_MSG("Cannot get DRM master: %s", strerror(errno));
		return FALSE;
	}

	depth = pScrn->depth;
#if USE_CRTC_AND_LCD
	if (pScrn->bitsPerPixel > 25)
		depth = 24;
#endif


#if DEBUG
	ERROR_STR("\tbitsPerPixel=%d, depth=%d, defaultVisual=%s"
	       "\tmask: %x,%x,%x, offset: %d,%d,%d",
	       pScrn->bitsPerPixel,
	       pScrn->depth,
	       xf86GetVisualName(pScrn->defaultVisual),
	       pScrn->mask.red,pScrn->mask.green,pScrn->mask.blue,
	       pScrn->offset.red,pScrn->offset.green,pScrn->offset.blue);
#endif

	if (fPtr->isFBDevHW) {
		if (NULL == (fPtr->fbmem = FBTurboHWMapVidmem(pScrn))) {
		        ERROR_MSG("mapping of video memory"
				   " failed");
			return FALSE;
		}
		fPtr->fboff = FBTurboHWLinearOffset(pScrn);

		fbdevHWSave(pScrn);

		DEBUG_MSG(1, "FBTurboHWModeInit");
		if (!FBTurboHWModeInit(pScrn, pScrn->currentMode)) {
			ERROR_MSG("mode initialization failed");
			return FALSE;
		}

		fbdevHWSaveScreen(pScreen, SCREEN_SAVER_ON);
		fbdevHWAdjustFrame(ADJUST_FRAME_ARGS(pScrn, 0, 0));
	} else {
		fPtr->fbmem = NULL;
		fPtr->fboff = 0;
	}

	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	/* need to point to new screen on server regeneration */
	for (j = 0; j < xf86_config->num_crtc; j++)
		xf86_config->crtc[j]->scrn = pScrn;
	for (j = 0; j < xf86_config->num_output; j++)
		xf86_config->output[j]->scrn = pScrn;

	/* mi layer */
	DEBUG_MSG(1, "miClearVisualTypes");
	miClearVisualTypes();

	if (pScrn->bitsPerPixel > 8) {
		DEBUG_MSG(1, "miSetVisualTypes %i", depth);
		if (!miSetVisualTypes(depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
			ERROR_MSG("visual type setup failed"
				   " for %d bits per pixel [1]",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	} else {
		DEBUG_MSG(1, "miSetVisualTypes %i", depth);
		if (!miSetVisualTypes(depth,
				      miGetDefaultVisualMask(depth),
				      pScrn->rgbBits, pScrn->defaultVisual)) {
			ERROR_MSG("visual type setup failed"
				   " for %d bits per pixel [2]",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	}

	/* Also add a 32-bit depth XRGB8888 visual */
	if (!miSetVisualTypes(32, miGetDefaultVisualMask(pScrn->depth),
				pScrn->rgbBits, pScrn->defaultVisual)) {
		WARNING_MSG("Cannot initialize a depth-32 visual");
	} else {
		INFO_MSG("Initialized a depth-32 visual for XRGB8888");
	}

	DEBUG_MSG(1, "miSetPixmapDepths");
	if (!miSetPixmapDepths()) {
	  ERROR_MSG("pixmap depth setup failed");
	  return FALSE;
	}

	if(fPtr->rotate==FBDEV_ROTATE_CW || fPtr->rotate==FBDEV_ROTATE_CCW)
	{
	  int tmp = pScrn->virtualX;
	  pScrn->virtualX = pScrn->displayWidth = pScrn->virtualY;
	  pScrn->virtualY = tmp;
	} else if (!fPtr->shadowFB) {
		/* FIXME: this doesn't work for all cases, e.g. when each scanline
			has a padding which is independent from the depth (controlfb) */
		pScrn->displayWidth = FBTurboHWGetLineLength(pScrn) /
				      (pScrn->bitsPerPixel / 8);

		if (pScrn->displayWidth != pScrn->virtualX) {
			INFO_MSG(
				   "Pitch updated to %d after ModeInit",
				   pScrn->displayWidth);
		}
	}

	if(fPtr->rotate && !fPtr->PointerMoved) {
		fPtr->PointerMoved = pScrn->PointerMoved;
		pScrn->PointerMoved = FBDevPointerMoved;
	}

	fPtr->NoFlip = FALSE;

	if (xf86ReturnOptValBool(fPtr->Options, OPTION_DRI2, TRUE)) {
		if (!InitFBTurboPriv(pScreen, pScrn, fPtr->drmFD)) {
			ERROR_STR("%s: init privates failed!", __func__);
		}
	}

	fPtr->fbstart = fPtr->fbmem + fPtr->fboff;
	fPtr->scanout = FBTURBO_BO_INVALID_HANDLE;
	fPtr->scanout_ptr = NULL;

	if (fPtr->shadowFB) {
	    if (fPtr->bo_ops && fPtr->UseDumb) {
		fPtr->scanout = fPtr->bo_ops->new(fPtr->bo_dev, pScrn->virtualX,
			pScrn->virtualY, pScrn->bitsPerPixel, pScrn->bitsPerPixel,
			0, FBTURBO_BO_TYPE_DEFAULT);
		fPtr->scanout_ptr = fPtr->bo_ops->map(fPtr->scanout);
		fPtr->shadow = fPtr->scanout_ptr;
	    }

	    if (fPtr->shadow)
		INFO_MSG("Created shadow bo");
	    else
	        fPtr->shadow = calloc(1, pScrn->virtualX * pScrn->virtualY *
				  pScrn->bitsPerPixel);

	    if (!fPtr->shadow) {
		ERROR_MSG(
			   "Failed to allocate shadow framebuffer");
		return FALSE;
	    }

	} else {
	    if (fPtr->bo_ops && fPtr->UseDumb) {
		fPtr->scanout = fPtr->bo_ops->new(fPtr->bo_dev, pScrn->virtualX,
			pScrn->virtualY, pScrn->bitsPerPixel, pScrn->bitsPerPixel,
			ARMSOC_CREATE_PIXMAP_SCANOUT, FBTURBO_BO_TYPE_DEFAULT);
		fPtr->scanout_ptr = fPtr->bo_ops->map(fPtr->scanout);
		fPtr->fbstart = fPtr->scanout_ptr;
		if (!fPtr->fbmem)
			fPtr->fbmem = fPtr->scanout_ptr;
	    }

	    if (!fPtr->fbstart) {
		ERROR_MSG(
			   "Failed to allocate scanout buffer");
		return FALSE;
	    }
	}

	if (fPtr->bo_ops && fPtr->scanout_ptr) {
		pScrn->displayWidth = fPtr->bo_ops->get_pitch(fPtr->scanout) /
				((pScrn->bitsPerPixel+7) / 8);
	}

	switch ((type = FBTurboHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel) {
		case 8:
		case 16:
		case 24:
		case 32:
			ret = fbScreenInit(pScreen, fPtr->shadowFB ? fPtr->shadow
					   : fPtr->fbstart, pScrn->virtualX,
					   pScrn->virtualY, pScrn->xDpi,
					   pScrn->yDpi, pScrn->displayWidth,
					   pScrn->bitsPerPixel);
			init_picture = 1;
			break;
	 	default:
			ERROR_MSG(
				   "internal error: invalid number of bits per"
				   " pixel (%d) encountered in"
				   " FBDevScreenInit()", pScrn->bitsPerPixel);
			ret = FALSE;
			break;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		ERROR_MSG(
		           "internal error: interleaved planes are not yet "
			   "supported by the fbdev driver");
		ret = FALSE;
		break;
	case FBDEVHW_TEXT:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		ERROR_MSG(
		           "internal error: text mode is not supported by the "
			   "fbdev driver");
		ret = FALSE;
		break;
	case FBDEVHW_VGA_PLANES:
		/* Not supported yet */
		ERROR_MSG(
		           "internal error: EGA/VGA Planes are not yet "
			   "supported by the fbdev driver");
		ret = FALSE;
		break;
	default:
		ERROR_MSG(
		           "internal error: unrecognised hardware type (%d) "
			   "encountered in FBDevScreenInit()", type);
		ret = FALSE;
		break;
	}
	if (!ret)
		return FALSE;

	if (pScrn->bitsPerPixel > 8) {
		/* Fixup RGB ordering */
		visual = pScreen->visuals + pScreen->numVisuals;
		while (--visual >= pScreen->visuals) {
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed   = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue  = pScrn->offset.blue;
				visual->redMask     = pScrn->mask.red;
				visual->greenMask   = pScrn->mask.green;
				visual->blueMask    = pScrn->mask.blue;
				visual->bitsPerRGBValue = pScrn->rgbBits;
				visual->ColormapEntries = 1 << pScrn->rgbBits;
			}
		}
	}

	/* must be after RGB ordering fixed */
	if (init_picture && !fbPictureInit(pScreen, NULL, 0))
		WARNING_MSG(
			   "Render extension initialisation failed");

	/*
	 * by default make use of backing store (the driver decides for which
	 * windows it is beneficial) if shadow is not enabled.
	 */
	useBackingStore = xf86ReturnOptValBool(fPtr->Options, OPTION_USE_BS,
	                                       !fPtr->shadowFB && !fPtr->UseEXA);
#ifndef __arm__
	/*
	 * right now we can only make "smart" decisions on ARM hardware,
	 * everything else (for example x86) would take a performance hit
	 * unless backing store is just used for all windows.
	 */
	forceBackingStore = useBackingStore;
#endif
	/* but still honour the settings from xorg.conf */
	forceBackingStore = xf86ReturnOptValBool(fPtr->Options, OPTION_FORCE_BS,
	                                         forceBackingStore);

	if (useBackingStore || forceBackingStore) {
		fPtr->backing_store_tuner_private =
			BackingStoreTuner_Init(pScreen, forceBackingStore);
	}

	/* initialize the 'CPU' backend */
	cpu_backend = cpu_backend_init(fPtr->fbmem, pScrn->videoRam);
	fPtr->cpu_backend_private = cpu_backend;

	/* try to load G2D kernel module before initializing sunxi-disp */
	if (!xf86LoadKernelModule("g2d_23"))
		INFO_MSG(
		           "can't load 'g2d_23' kernel module");

	fPtr->sunxi_disp_private = sunxi_disp_init(xf86FindOptionValue(
	                                fPtr->pEnt->device->options,"fbdev"),
	                                fPtr->fbmem);
	if ((!fPtr->sunxi_disp_private) && fPtr->fbmem) {
		INFO_MSG(
		           "failed to enable the use of sunxi display controller");
		fPtr->fb_copyarea_private = fb_copyarea_init(xf86FindOptionValue(
	                                fPtr->pEnt->device->options,"fbdev"),
	                                fPtr->fbmem);
	}

	if (!(accelmethod = xf86GetOptValString(fPtr->Options, OPTION_ACCELMETHOD)) ||
						strcasecmp(accelmethod, "g2d") == 0) {
		sunxi_disp_t *disp = fPtr->sunxi_disp_private;
		if (disp && disp->fd_g2d >= 0 &&
		    (fPtr->SunxiG2D_private = SunxiG2D_Init(pScreen, &disp->blt2d))) {
			disp->fallback_blt2d = &cpu_backend->blt2d;
			INFO_MSG( "enabled G2D acceleration");
		}
		else {
			INFO_MSG(
				"No sunxi-g2d hardware detected (check /dev/disp and /dev/g2d)");
			INFO_MSG(
				"G2D hardware acceleration can't be enabled");
		}
	}
	else {
		INFO_MSG(
			"G2D acceleration is disabled via AccelMethod option");
	}

	if (!fPtr->SunxiG2D_private && fPtr->fb_copyarea_private) {
		if (!(accelmethod = xf86GetOptValString(fPtr->Options, OPTION_ACCELMETHOD)) ||
						strcasecmp(accelmethod, "copyarea") == 0) {
			fb_copyarea_t *fb = fPtr->fb_copyarea_private;
			if ((fPtr->SunxiG2D_private = SunxiG2D_Init(pScreen, &fb->blt2d))) {
				fb->fallback_blt2d = &cpu_backend->blt2d;
				INFO_MSG(
				           "enabled fbdev copyarea acceleration");
			}
			else {
				INFO_MSG(
				           "failed to enable fbdev copyarea acceleration");
			}
		}
		else {
			INFO_MSG(
			           "fbdev copyarea acceleration is disabled via AccelMethod option");
		}
	}

	if (!fPtr->SunxiG2D_private && fPtr->fbmem && cpu_backend->cpuinfo->has_arm_vfp) {
		if ((fPtr->SunxiG2D_private = SunxiG2D_Init(pScreen, &cpu_backend->blt2d))) {
			INFO_MSG( "enabled VFP/NEON optimizations");
		}
	}

	if (fPtr->shadowFB && !FBDevShadowInit(pScreen)) {
	    ERROR_MSG(
		       "shadow framebuffer initialization failed");
	    return FALSE;
	}

	fPtr->CreateScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = FBDevCreateScreenResources;

	if (!fPtr->rotate)
	  FBDevDGAInit(pScrn, pScreen);
	else {
	  INFO_MSG( "display rotated; disabling DGA");
	  INFO_MSG( "using driver rotation; disabling "
			                "XRandR");
#if ABI_VIDEODRV_VERSION < SET_ABI_VERSION(24, 0)
	  xf86DisableRandR();
#endif
	  if (pScrn->bitsPerPixel == 24)
	    WARNING_MSG( "rotation might be broken at 24 "
                                             "bits per pixel");
	}

	xf86SetBlackWhitePixels(pScreen);
	xf86SetBackingStore(pScreen);
	DEBUG_MSG(1, "xf86SetSilkenMouse");
	xf86SetSilkenMouse(pScreen);

	/* software cursor */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

#if USE_CRTC_AND_LCD
#if 0
	DEBUG_MSG(1, "xf86SetDesiredModes");
	xf86SetDesiredModes(pScrn);
#endif

	pScrn->vtSema = TRUE;

	pScreen->SaveScreen = xf86SaveScreen;

	/* Wrap the current CloseScreen function */
	fPtr->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = FBDevCloseScreen;

	/* Wrap the current BlockHandler function */
	wrap(fPtr, pScreen, BlockHandler, FBDevBlockHandler_oneshot);

	DEBUG_MSG(1, "xf86CrtcScreenInit");
	if (!xf86CrtcScreenInit(pScreen))
	{
		ERROR_MSG("xf86CrtcScreenInit failed");
		return FALSE;
	}

	if (!FBTurboEnterVT(VT_FUNC_ARGS(0))) {
		ERROR_MSG("FBTurboEnterVT() failed!");
		return FALSE;
	}

#endif /* USE_CRTC_AND_LCD */

	/* colormap */
	switch ((type = FBTurboHWGetType(pScrn)))
	{
	/* XXX It would be simpler to use miCreateDefColormap() in all cases. */
	case FBDEVHW_PACKED_PIXELS:
		if (!miCreateDefColormap(pScreen)) {
			ERROR_MSG(
                                   "internal error: miCreateDefColormap failed "
				   "in FBDevScreenInit()");
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		ERROR_MSG(
		           "internal error: interleaved planes are not yet "
			   "supported by the fbdev driver");
		return FALSE;
	case FBDEVHW_TEXT:
		ERROR_MSG(
		           "internal error: text mode is not supported by "
			   "the fbdev driver");
		return FALSE;
	case FBDEVHW_VGA_PLANES:
		ERROR_MSG(
		           "internal error: EGA/VGA planes are not yet "
			   "supported by the fbdev driver");
		return FALSE;
	default:
		ERROR_MSG(
		           "internal error: unrecognised fbdev hardware type "
			   "(%d) encountered in FBDevScreenInit()", type);
		return FALSE;
	}

	flags = CMAP_PALETTED_TRUECOLOR;

	DEBUG_MSG(1, "xf86HandleColormaps");
	if (fPtr->isFBDevHW) {
		if(!xf86HandleColormaps(pScreen, 1 << pScrn->rgbBits, pScrn->rgbBits, FBTurboHWLoadPalette, 
					NULL, flags))
			return FALSE;
	} else {
		if (!xf86HandleColormaps(pScreen, 1 << pScrn->rgbBits, 10, fbdev_load_palette,
					 NULL, CMAP_PALETTED_TRUECOLOR | CMAP_RELOAD_ON_MODE_SWITCH))
			return FALSE;
	}

	DEBUG_MSG(1, "xf86DPMSInit");
	xf86DPMSInit(pScreen, xf86DPMSSet, 0);

#if XV
	fPtr->SunxiVideo_private = NULL;
	if (xf86ReturnOptValBool(fPtr->Options, OPTION_XV_OVERLAY, TRUE) &&
	fPtr->sunxi_disp_private) {
	    fPtr->SunxiVideo_private = SunxiVideo_Init(pScreen);
	    if (fPtr->SunxiVideo_private)
		INFO_MSG(
		           "using sunxi disp layers for X video extension");
	}
	else {
	    XF86VideoAdaptorPtr *ptr;

	    int n = xf86XVListGenericAdaptors(pScrn,&ptr);
	    if (n) {
		xf86XVScreenInit(pScreen,ptr,n);
	    }
	}
#endif

	if (fPtr->rotate == FBDEV_ROTATE_NONE &&
	    !xf86ReturnOptValBool(fPtr->Options, OPTION_SW_CURSOR, FALSE) &&
	     xf86ReturnOptValBool(fPtr->Options, OPTION_HW_CURSOR, TRUE)) {

	    fPtr->SunxiDispHardwareCursor_private = SunxiDispHardwareCursor_Init(
	                                pScreen);

	    if (fPtr->SunxiDispHardwareCursor_private)
		INFO_MSG(
		           "using hardware cursor");
	    else
		INFO_MSG(
		           "failed to enable hardware cursor");
	}

	if (xf86ReturnOptValBool(fPtr->Options, OPTION_DRI2, TRUE)) {
	    if (fPtr->UseEXA)
		fPtr->FBTurboEXA_private = InitNullEXA(pScreen, pScrn, fPtr->drmFD);

	    fPtr->FBTurboMaliDRI2_private = FBTurboMaliDRI2_Init(pScreen,
		xf86ReturnOptValBool(fPtr->Options, OPTION_DRI2_OVERLAY, TRUE),
		xf86ReturnOptValBool(fPtr->Options, OPTION_SWAPBUFFERS_WAIT, TRUE),
		fPtr->UseDumb);

	    if (fPtr->FBTurboMaliDRI2_private) {
		if (fPtr->UseDumb)
		    INFO_MSG(
			   "using DRI2 integration for SOC GPU (dumb buffers)");
		else
		    INFO_MSG(
		           "using DRI2 integration for SOC GPU (UMP buffers)");
		INFO_MSG(
		           "Mali binary drivers can only accelerate EGL/GLES");
		INFO_MSG(
		           "so AIGLX/GLX is expected to fail or fallback to software");
	    }
	    else {
		INFO_MSG(
		           "failed to enable DRI2 integration for Mali GPU");
	    }
	}
	else {
	    INFO_MSG(
	               "DRI2 integration for Mali GPU is disabled in xorg.conf");
	}

	TRACE_EXIT("FBDevScreenInit");

	return TRUE;
}

static Bool
FBDevCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (fPtr->FBTurboMaliDRI2_private) {
	    FBTurboMaliDRI2_Close(pScreen);
	    free(fPtr->FBTurboMaliDRI2_private);
	    fPtr->FBTurboMaliDRI2_private = NULL;
	}

	if (fPtr->SunxiDispHardwareCursor_private) {
	    SunxiDispHardwareCursor_Close(pScreen);
	    free(fPtr->SunxiDispHardwareCursor_private);
	    fPtr->SunxiDispHardwareCursor_private = NULL;
	}

#if XV
	if (fPtr->SunxiVideo_private) {
	    SunxiVideo_Close(pScreen);
	    free(fPtr->SunxiVideo_private);
	    fPtr->SunxiVideo_private = NULL;
	}
#endif

	if (fPtr->FBTurboEXA_private)
		if (fPtr->FBTurboEXA_private->CloseScreen)
			fPtr->FBTurboEXA_private->CloseScreen(CLOSE_SCREEN_ARGS);

	if (fPtr->isFBDevHW) {
		fbdevHWRestore(pScrn);
		FBTurboHWUnmapVidmem(pScrn);
	}

	if (fPtr->shadow) {
	    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));
	    if (fPtr->shadow != fPtr->scanout_ptr)
	        free(fPtr->shadow);
	    fPtr->shadow = NULL;
	}

	if (fPtr->bo_ops && fPtr->bo_ops->valid(fPtr->scanout)) {
	    fPtr->bo_ops->unmap(fPtr->scanout);
	    fPtr->bo_ops->release(fPtr->scanout);
	    free(fPtr->scanout);
	}

	if (fPtr->SunxiG2D_private) {
	    SunxiG2D_Close(pScreen);
	    free(fPtr->SunxiG2D_private);
	    fPtr->SunxiG2D_private = NULL;
	}
	if (fPtr->fb_copyarea_private) {
	    fb_copyarea_close(fPtr->fb_copyarea_private);
	    fPtr->fb_copyarea_private = NULL;
	}
	if (fPtr->sunxi_disp_private) {
	    sunxi_disp_close(fPtr->sunxi_disp_private);
	    fPtr->sunxi_disp_private = NULL;
	}
	if (fPtr->cpu_backend_private) {
	    cpu_backend_close(fPtr->cpu_backend_private);
	    fPtr->cpu_backend_private = NULL;
	}

	if (fPtr->backing_store_tuner_private) {
	    BackingStoreTuner_Close(pScreen);
	    free(fPtr->backing_store_tuner_private);
	    fPtr->backing_store_tuner_private = NULL;
	}

	if (fPtr->pDGAMode) {
	  free(fPtr->pDGAMode);
	  fPtr->pDGAMode = NULL;
	  fPtr->nDGAMode = 0;
	}

	pScrn->displayWidth = 0;

	if (pScrn->vtSema == TRUE)
		FBTurboLeaveVT(VT_FUNC_ARGS(0));

	pScrn->vtSema = FALSE;

	pScreen->CreateScreenResources = fPtr->CreateScreenResources;
	pScreen->CloseScreen = fPtr->CloseScreen;
	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static void
FBDevFreeScreen(FREE_SCREEN_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	TRACE_ENTER();

	if (!fPtr) {
		/* This can happen if a Screen is deleted after Probe(): */
		return;
	}

	if (fPtr->FBTurboEXA_private) {
		if (fPtr->FBTurboEXA_private->FreeScreen)
			fPtr->FBTurboEXA_private->FreeScreen(
					FREE_SCREEN_ARGS(pScrn));
	}

	if (fPtr->bo_ops)
		fPtr->bo_ops->close(fPtr->bo_dev);

	FBDevFreeRec(pScrn);

	TRACE_EXIT();
}

static void
FBDevBlockHandler(BLOCKHANDLER_ARGS_DECL)
{
	SCREEN_PTR(arg);
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);

	swap(fPtr, pScreen, BlockHandler);
	(*pScreen->BlockHandler) (BLOCKHANDLER_ARGS);
	swap(fPtr, pScreen, BlockHandler);
}

static void
FBDevBlockHandler_oneshot(ScreenPtr pScreen, void *pTimeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    TRACE_ENTER();

    pScreen->BlockHandler = FBDevBlockHandler;
    FBDevBlockHandler(pScreen, pTimeout);

    fbdev_set_desired_modes(pScrn, fPtr, TRUE);

    TRACE_EXIT();
}


/***********************************************************************
 * Shadow stuff
 ***********************************************************************/

static void *
FBDevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
		 CARD32 *size, void *closure)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (!pScrn->vtSema)
      return NULL;

    if (fPtr->lineLength)
      *size = fPtr->lineLength;
    else
      *size = fPtr->lineLength = FBTurboHWGetLineLength(pScrn);

    return ((CARD8 *)fPtr->fbstart + row * fPtr->lineLength + offset);
}

static void
FBDevPointerMoved(SCRN_ARG_TYPE arg, int x, int y)
{
    SCRN_INFO_PTR(arg);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    int newX, newY;

    switch (fPtr->rotate)
    {
    case FBDEV_ROTATE_CW:
	/* 90 degrees CW rotation. */
	newX = pScrn->pScreen->height - y - 1;
	newY = x;
	break;

    case FBDEV_ROTATE_CCW:
	/* 90 degrees CCW rotation. */
	newX = y;
	newY = pScrn->pScreen->width - x - 1;
	break;

    case FBDEV_ROTATE_UD:
	/* 180 degrees UD rotation. */
	newX = pScrn->pScreen->width - x - 1;
	newY = pScrn->pScreen->height - y - 1;
	break;

    default:
	/* No rotation. */
	newX = x;
	newY = y;
	break;
    }

    /* Pass adjusted pointer coordinates to wrapped PointerMoved function. */
    (*fPtr->PointerMoved)(arg, newX, newY);
}


#if 0
/***********************************************************************
 * DGA stuff
 ***********************************************************************/
static Bool FBDevDGAOpenFramebuffer(ScrnInfoPtr pScrn, char **DeviceName,
				   unsigned char **ApertureBase,
				   int *ApertureSize, int *ApertureOffset,
				   int *flags);
static Bool FBDevDGASetMode(ScrnInfoPtr pScrn, DGAModePtr pDGAMode);
static void FBDevDGASetViewport(ScrnInfoPtr pScrn, int x, int y, int flags);

static Bool
FBDevDGAOpenFramebuffer(ScrnInfoPtr pScrn, char **DeviceName,
		       unsigned char **ApertureBase, int *ApertureSize,
		       int *ApertureOffset, int *flags)
{
    *DeviceName = NULL;		/* No special device */
    *ApertureBase = (unsigned char *)(pScrn->memPhysBase);
    *ApertureSize = pScrn->videoRam;
    *ApertureOffset = pScrn->fbOffset;
    *flags = 0;

    return TRUE;
}

static Bool
FBDevDGASetMode(ScrnInfoPtr pScrn, DGAModePtr pDGAMode)
{
    DisplayModePtr pMode;
    int frameX0, frameY0;

    if (pDGAMode) {
	pMode = pDGAMode->mode;
	frameX0 = frameY0 = 0;
    }
    else {
	if (!(pMode = pScrn->currentMode))
	    return TRUE;

	frameX0 = pScrn->frameX0;
	frameY0 = pScrn->frameY0;
    }

    if (!(*pScrn->SwitchMode)(SWITCH_MODE_ARGS(pScrn, pMode)))
	return FALSE;
    (*pScrn->AdjustFrame)(ADJUST_FRAME_ARGS(pScrn, frameX0, frameY0));

    return TRUE;
}

static void
FBDevDGASetViewport(ScrnInfoPtr pScrn, int x, int y, int flags)
{
    (*pScrn->AdjustFrame)(ADJUST_FRAME_ARGS(pScrn, x, y));
}

static int
FBDevDGAGetViewport(ScrnInfoPtr pScrn)
{
    return (0);
}

static DGAFunctionRec FBDevDGAFunctions =
{
    FBDevDGAOpenFramebuffer,
    NULL,       /* CloseFramebuffer */
    FBDevDGASetMode,
    FBDevDGASetViewport,
    FBDevDGAGetViewport,
    NULL,       /* Sync */
    NULL,       /* FillRect */
    NULL,       /* BlitRect */
    NULL,       /* BlitTransRect */
};

static void
FBDevDGAAddModes(ScrnInfoPtr pScrn)
{
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    DisplayModePtr pMode = pScrn->modes;
    DGAModePtr pDGAMode;

    do {
	if (!pMode)
	    break;

	pDGAMode = realloc(fPtr->pDGAMode,
		           (fPtr->nDGAMode + 1) * sizeof(DGAModeRec));
	if (!pDGAMode)
	    break;

	fPtr->pDGAMode = pDGAMode;
	pDGAMode += fPtr->nDGAMode;
	(void)memset(pDGAMode, 0, sizeof(DGAModeRec));

	++fPtr->nDGAMode;
	pDGAMode->mode = pMode;
	pDGAMode->flags = DGA_CONCURRENT_ACCESS | DGA_PIXMAP_AVAILABLE;
	pDGAMode->byteOrder = pScrn->imageByteOrder;
	pDGAMode->depth = pScrn->depth;
	pDGAMode->bitsPerPixel = pScrn->bitsPerPixel;
	pDGAMode->red_mask = pScrn->mask.red;
	pDGAMode->green_mask = pScrn->mask.green;
	pDGAMode->blue_mask = pScrn->mask.blue;
	pDGAMode->visualClass = pScrn->bitsPerPixel > 8 ?
	    TrueColor : PseudoColor;
	pDGAMode->xViewportStep = 1;
	pDGAMode->yViewportStep = 1;
	pDGAMode->viewportWidth = pMode->HDisplay;
	pDGAMode->viewportHeight = pMode->VDisplay;

	if (fPtr->lineLength)
	  pDGAMode->bytesPerScanline = fPtr->lineLength;
	else
	  pDGAMode->bytesPerScanline = fPtr->lineLength = FBTurboHWGetLineLength(pScrn);

	pDGAMode->imageWidth = pMode->HDisplay;
	pDGAMode->imageHeight =  pMode->VDisplay;
	pDGAMode->pixmapWidth = pDGAMode->imageWidth;
	pDGAMode->pixmapHeight = pDGAMode->imageHeight;
	pDGAMode->maxViewportX = pScrn->virtualX -
				    pDGAMode->viewportWidth;
	pDGAMode->maxViewportY = pScrn->virtualY -
				    pDGAMode->viewportHeight;

	pDGAMode->address = fPtr->fbstart;

	pMode = pMode->next;
    } while (pMode != pScrn->modes);
}
#endif

static Bool
FBDevDGAInit(ScrnInfoPtr pScrn, ScreenPtr pScreen)
{
#if 0
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (pScrn->depth < 8)
	return FALSE;

    if (!fPtr->nDGAMode)
	FBDevDGAAddModes(pScrn);

    return (DGAInit(pScreen, &FBDevDGAFunctions,
	    fPtr->pDGAMode, fPtr->nDGAMode));
#else
    return TRUE;
#endif
}

static Bool
FBDevDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    xorgHWFlags *flag;
    
    switch (op) {
	case GET_REQUIRED_HW_INTERFACES:
	    flag = (CARD32*)ptr;
	    (*flag) = 0;
	    return TRUE;
	default:
	    return FALSE;
    }
}
