/*
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

#include <errno.h>
#include <sys/ioctl.h>
#include "xorg-server.h"
#include "xf86.h"
#include "xf86Crtc.h"
#include "xf86drmMode.h"

#include "fb_debug.h"
#include "fbdev_priv.h"
#include "fbdev_lcd.h"

struct fbdev_lcd_rec {
	int fd;
	drmModeResPtr mode_res;
	Bool isDummy;
};

struct fbdev_lcd_crtc_private_rec {
	struct fbdev_lcd_rec *fbdev_lcd;
	uint32_t crtc_id;
};

struct fbdev_lcd_output_priv {
	struct fbdev_lcd_rec *fbdev_lcd;
	int output_id;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr *encoders;
};

static void fbdev_lcd_output_dpms(xf86OutputPtr output, int mode);

static Bool
fbdev_lcd_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
		Rotation rotation, int x, int y);

static void fbdev_lcd_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
	struct fbdev_lcd_crtc_private_rec *fbdev_lcd_crtc = crtc->driver_private;
	struct fbdev_lcd_rec *fbdev_lcd;
	ScrnInfoPtr pScrn = crtc->scrn;

	TRACE_ENTER();

	if (!fbdev_lcd_crtc) {
		INFO_MSG("%s: not a fbdev_lcd crtc", __func__);
		return;
	}

	fbdev_lcd = fbdev_lcd_crtc->fbdev_lcd;

	if (fbdev_lcd->isDummy) {
		TRACE_EXIT();
		return;
	}

	DEBUG_MSG(1, "Setting dpms mode %d on crtc %d", mode, fbdev_lcd_crtc->crtc_id);

	switch (mode) {
	case DPMSModeOn:
		fbdev_lcd_set_mode_major(crtc, &crtc->mode, crtc->rotation, crtc->x, crtc->y);
		break;

	/* unimplemented modes fall through to the next lowest mode */
	case DPMSModeStandby:
	case DPMSModeSuspend:
	case DPMSModeOff:
		if (drmModeSetCrtc(fbdev_lcd->fd, fbdev_lcd_crtc->crtc_id, 0, 0, 0, 0, 0, NULL)) {
			ERROR_MSG("drm failed to disable crtc %d", fbdev_lcd_crtc->crtc_id);
		} else {
			int i;
			xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

			/* set dpms off for all outputs for this crtc */
			for (i = 0; i < xf86_config->num_output; i++) {
				xf86OutputPtr output = xf86_config->output[i];
				if (output->crtc != crtc)
					continue;
				fbdev_lcd_output_dpms(output, mode);
			}
		}
		break;
	default:
		ERROR_MSG("bad dpms mode %d for crtc %d", mode, fbdev_lcd_crtc->crtc_id);
		return;
	}

	TRACE_EXIT();
}

static Bool fbdev_lcd_crtc_lock(xf86CrtcPtr crtc)
{
	IGNORE(crtc);

	return TRUE;
}

static void fbdev_lcd_crtc_unlock(xf86CrtcPtr crtc)
{
	IGNORE(crtc);
}

static Bool fbdev_lcd_crtc_mode_fixup(xf86CrtcPtr crtc, DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
	IGNORE(crtc);
	IGNORE(mode);
	IGNORE(adjusted_mode);

	return TRUE;
}

static void fbdev_lcd_crtc_prepare(xf86CrtcPtr crtc)
{
	IGNORE(crtc);
}

static void fbdev_lcd_crtc_mode_set(xf86CrtcPtr crtc, DisplayModePtr mode, DisplayModePtr adjusted_mode, int x, int y)
{
	IGNORE(crtc);
	IGNORE(mode);
	IGNORE(adjusted_mode);
	IGNORE(x);
	IGNORE(y);
}

static void fbdev_lcd_crtc_commit(xf86CrtcPtr crtc)
{
	IGNORE(crtc);
}

static void fbdev_lcd_crtc_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, CARD16 *blue, int size)
{
	IGNORE(crtc);
	IGNORE(red);
	IGNORE(green);
	IGNORE(blue);
	IGNORE(size);
}

static void fbdev_lcd_crtc_set_origin(xf86CrtcPtr crtc, int x, int y)
{
	IGNORE(crtc);
	IGNORE(x);
	IGNORE(y);
}

static void
fbdev_lcd_convert_to_kmode(ScrnInfoPtr pScrn, drmModeModeInfo *kmode,
		DisplayModePtr mode)
{
	memset(kmode, 0, sizeof(*kmode));

	kmode->clock = mode->Clock;
	kmode->hdisplay = mode->HDisplay;
	kmode->hsync_start = mode->HSyncStart;
	kmode->hsync_end = mode->HSyncEnd;
	kmode->htotal = mode->HTotal;
	kmode->hskew = mode->HSkew;

	kmode->vdisplay = mode->VDisplay;
	kmode->vsync_start = mode->VSyncStart;
	kmode->vsync_end = mode->VSyncEnd;
	kmode->vtotal = mode->VTotal;
	kmode->vscan = mode->VScan;

	kmode->flags = mode->Flags;
	if (mode->name)
		strncpy(kmode->name, mode->name, DRM_DISPLAY_MODE_LEN);
	kmode->name[DRM_DISPLAY_MODE_LEN-1] = 0;
}

static Bool
fbdev_lcd_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
		Rotation rotation, int x, int y)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	struct fbdev_lcd_crtc_private_rec *fbdev_lcd_crtc = crtc->driver_private;
	struct fbdev_lcd_rec *fbdev_lcd;
	uint32_t *output_ids = NULL;
	int output_count = 0;
	int ret = TRUE;
	int err;
	int i;
	uint32_t fb_id = 0;
	drmModeModeInfo kmode;
	drmModeCrtcPtr newcrtc = NULL;

	TRACE_ENTER();

	if (!fbdev_lcd_crtc) {
		INFO_MSG("%s: not a fbdev_lcd crtc", __func__);
		return FALSE;
	}

	fbdev_lcd = fbdev_lcd_crtc->fbdev_lcd;

	if (fbdev_lcd->isDummy) {
		TRACE_EXIT();
		return TRUE;
	}

	if (fPtr->bo_ops->valid(fPtr->scanout)) {
		fb_id = fPtr->bo_ops->get_fb(fPtr->scanout);

		if (fb_id == 0) {
			DEBUG_MSG(1, "create framebuffer: %dx%d",
					pScrn->virtualX, pScrn->virtualY);

			err = fPtr->bo_ops->add_fb(fPtr->scanout);
			if (err) {
				ERROR_MSG(
					"Failed to add framebuffer to the scanout buffer");
				return FALSE;
			}

			fb_id = fPtr->bo_ops->get_fb(fPtr->scanout);
			if (0 == fb_id)
				WARNING_MSG("Could not get fb for scanout");
		}
	}

	/* Set the new mode: */
	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;
	crtc->rotation = rotation;

	output_ids = calloc(xf86_config->num_output, sizeof *output_ids);
	if (!output_ids) {
		ERROR_MSG(
				"memory allocation failed in fbdev_lcd_set_mode_major()");
		ret = FALSE;
		goto cleanup;
	}

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		struct fbdev_lcd_output_priv *fbdev_lcd_output;

		if (output->crtc != crtc)
			continue;

		fbdev_lcd_output = output->driver_private;
		output_ids[output_count] =
				fbdev_lcd_output->connector->connector_id;
		output_count++;
	}

	if (!xf86CrtcRotate(crtc)) {
		ERROR_MSG(
				"failed to assign rotation in fbdev_lcd_set_mode_major()");
		ret = FALSE;
		goto cleanup;
	}

	if (crtc->funcs->gamma_set)
		crtc->funcs->gamma_set(crtc, crtc->gamma_red, crtc->gamma_green,
				       crtc->gamma_blue, crtc->gamma_size);

	if (0 == fb_id) {
		TRACE_EXIT();
		return TRUE;
	}

	fbdev_lcd_convert_to_kmode(crtc->scrn, &kmode, mode);

	err = drmModeSetCrtc(fbdev_lcd->fd, fbdev_lcd_crtc->crtc_id,
			fb_id, x, y, output_ids, output_count, &kmode);
	if (err) {
		ERROR_MSG(
				"drm failed to set mode: %s", strerror(-err));

		ret = FALSE;
		goto cleanup;
	}

	/* get the actual crtc info */
	newcrtc = drmModeGetCrtc(fbdev_lcd->fd, fbdev_lcd_crtc->crtc_id);
	if (!newcrtc) {
		ERROR_MSG("couldn't get actual mode back");

		ret = FALSE;
		goto cleanup;
	}

	if (kmode.hdisplay != newcrtc->mode.hdisplay ||
		kmode.vdisplay != newcrtc->mode.vdisplay) {

		ERROR_MSG(
			"drm did not set requested mode! (requested %dx%d, actual %dx%d)",
			kmode.hdisplay, kmode.vdisplay,
			newcrtc->mode.hdisplay,
			newcrtc->mode.vdisplay);

		ret = FALSE;
			goto cleanup;
	}

	ret = TRUE;

	/* Turn on any outputs on this crtc that may have been disabled: */
	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];

		if (output->crtc != crtc)
			continue;

		fbdev_lcd_output_dpms(output, DPMSModeOn);
	}

cleanup:
	if (newcrtc)
		drmModeFreeCrtc(newcrtc);

	if (output_ids)
		free(output_ids);

	TRACE_EXIT();
	return ret;
}

static const xf86CrtcFuncsRec fbdev_lcd_crtc_funcs =
{
	.dpms = fbdev_lcd_crtc_dpms,
	.save = NULL,
	.restore = NULL,
	.lock = fbdev_lcd_crtc_lock,
	.unlock = fbdev_lcd_crtc_unlock,
	.mode_fixup = fbdev_lcd_crtc_mode_fixup,
	.prepare = fbdev_lcd_crtc_prepare,
	.mode_set = fbdev_lcd_crtc_mode_set,
	.commit = fbdev_lcd_crtc_commit,
	.gamma_set = fbdev_lcd_crtc_gamma_set,
	.shadow_allocate = NULL,
	.shadow_create = NULL,
	.shadow_destroy = NULL,
	.set_cursor_colors = NULL,
	.set_cursor_position = NULL,
	.show_cursor = NULL,
	.hide_cursor = NULL,
	.load_cursor_image = NULL,
	.load_cursor_argb = NULL,
	.destroy = NULL,
	.set_mode_major = fbdev_lcd_set_mode_major,
	.set_origin = fbdev_lcd_crtc_set_origin,
};

static void
fbdev_lcd_output_dpms(xf86OutputPtr output, int mode)
{
	ScrnInfoPtr pScrn = output->scrn;
	FBDevPtr fPtr = FBDEVPTR(output->scrn);
	struct fbdev_lcd_output_priv *fbdev_lcd_output = output->driver_private;
	drmModeConnectorPtr connector = fbdev_lcd_output->connector;
	drmModePropertyPtr prop;
	struct fbdev_lcd_rec *fbdev_lcd = fbdev_lcd_output->fbdev_lcd;
	int mode_id = -1, i;

	TRACE_ENTER();

	if (fbdev_lcd->isDummy) {
		if (mode == DPMSModeOn)
		{
			ioctl(fPtr->fb_lcd_fd, FBIOBLANK, FB_BLANK_UNBLANK);
		}
		else if (mode == DPMSModeOff)
		{
			ioctl(fPtr->fb_lcd_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
		}

		TRACE_EXIT();
		return;
	}

	for (i = 0; i < connector->count_props; i++) {
		prop = drmModeGetProperty(fbdev_lcd->fd, connector->props[i]);
		if (!prop)
			continue;
		if ((prop->flags & DRM_MODE_PROP_ENUM) &&
		    !strcmp(prop->name, "DPMS")) {
			mode_id = connector->props[i];
			drmModeFreeProperty(prop);
			break;
		}
		drmModeFreeProperty(prop);
	}

	if (mode_id < 0)
		return;

	drmModeConnectorSetProperty(fbdev_lcd->fd, connector->connector_id,
			mode_id, mode);

	TRACE_EXIT();
}

static void fbdev_lcd_output_save(xf86OutputPtr output)
{
	IGNORE(output);
}

static void fbdev_lcd_output_restore(xf86OutputPtr output)
{
	IGNORE(output);
}

static int fbdev_lcd_output_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
	ScrnInfoPtr pScrn = output->scrn;
	IGNORE(output);
	IGNORE(pMode);

	/* return MODE_ERROR in case of unsupported mode */
	INFO_MSG("Mode %i x %i valid", pMode->HDisplay, pMode->VDisplay);

	return MODE_OK;
}

static Bool fbdev_lcd_output_mode_fixup(xf86OutputPtr output, DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
	IGNORE(output);
	IGNORE(mode);
	IGNORE(adjusted_mode);

	return TRUE;
}

static void fbdev_lcd_output_prepare(xf86OutputPtr output)
{
	IGNORE(output);
}

static void fbdev_lcd_output_commit(xf86OutputPtr output)
{
	output->funcs->dpms(output, DPMSModeOn);
}

static void fbdev_lcd_output_mode_set(xf86OutputPtr output, DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
	ScrnInfoPtr pScrn = output->scrn;
	FBDevPtr fPtr = FBDEVPTR(output->scrn);

	IGNORE(output);
	IGNORE(mode);
	IGNORE(adjusted_mode);

	if (ioctl(fPtr->fb_lcd_fd, FBIOGET_VSCREENINFO, &fPtr->fb_lcd_var) < 0)
	{
		INFO_MSG("Unable to get VSCREENINFO");
	}

	fPtr->fb_lcd_var.xres = mode->HDisplay;
	fPtr->fb_lcd_var.yres = mode->VDisplay;
	fPtr->fb_lcd_var.xres_virtual = mode->HDisplay;
	fPtr->fb_lcd_var.yres_virtual = mode->VDisplay * 2;
	INFO_MSG("Changing mode to %i %i %i %i", fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, fPtr->fb_lcd_var.xres_virtual, fPtr->fb_lcd_var.yres_virtual);

	if (ioctl(fPtr->fb_lcd_fd, FBIOPUT_VSCREENINFO, &fPtr->fb_lcd_var) < 0)
	{
		INFO_MSG("Unable to set mode!");
	}

}

static xf86OutputStatus fbdev_lcd_output_detect(xf86OutputPtr output)
{
	IGNORE(output);

	return XF86OutputStatusConnected;
}

void fbdev_copy_mode(DisplayModePtr source_mode_ptr, DisplayModePtr mode_ptr)
{
	mode_ptr->status = source_mode_ptr->status;
	mode_ptr->type = source_mode_ptr->type;

	mode_ptr->Clock = source_mode_ptr->Clock;

	mode_ptr->HDisplay = source_mode_ptr->HDisplay;
	mode_ptr->HSyncStart = source_mode_ptr->HSyncStart;
	mode_ptr->HSyncEnd = source_mode_ptr->HSyncEnd;
	mode_ptr->HTotal = source_mode_ptr->HTotal;

	mode_ptr->HSkew = source_mode_ptr->HSkew;

	mode_ptr->VDisplay = source_mode_ptr->VDisplay;
	mode_ptr->VSyncStart = source_mode_ptr->VSyncStart;
	mode_ptr->VSyncEnd = source_mode_ptr->VSyncEnd;
	mode_ptr->VTotal = source_mode_ptr->VTotal;

	mode_ptr->VScan = source_mode_ptr->VScan;
	mode_ptr->Flags = source_mode_ptr->Flags;

	mode_ptr->ClockIndex = source_mode_ptr->ClockIndex;

	mode_ptr->CrtcHDisplay = source_mode_ptr->CrtcHDisplay;
	mode_ptr->CrtcHSyncStart = source_mode_ptr->CrtcHSyncStart;
	mode_ptr->CrtcHSyncEnd = source_mode_ptr->CrtcHSyncEnd;
	mode_ptr->CrtcHTotal = source_mode_ptr->CrtcHTotal;

	mode_ptr->CrtcHSkew = source_mode_ptr->CrtcHSkew;

	mode_ptr->CrtcVDisplay = source_mode_ptr->CrtcVDisplay;
	mode_ptr->CrtcVSyncStart = source_mode_ptr->CrtcVSyncStart;
	mode_ptr->CrtcVSyncEnd = source_mode_ptr->CrtcVSyncEnd;
	mode_ptr->CrtcVTotal = source_mode_ptr->CrtcVTotal;

	mode_ptr->CrtcHBlankStart = source_mode_ptr->CrtcHBlankStart;
	mode_ptr->CrtcHBlankEnd = source_mode_ptr->CrtcHBlankEnd;
	mode_ptr->CrtcVBlankStart = source_mode_ptr->CrtcVBlankStart;
	mode_ptr->CrtcVBlankEnd = source_mode_ptr->CrtcVBlankEnd;

	mode_ptr->CrtcHAdjusted = source_mode_ptr->CrtcHAdjusted;
	mode_ptr->CrtcVAdjusted = source_mode_ptr->CrtcVAdjusted;

	mode_ptr->HSync = source_mode_ptr->HSync;
	mode_ptr->VRefresh = source_mode_ptr->VRefresh;
}

void fbdev_fill_mode(DisplayModePtr mode_ptr, int xres, int yres, float vrefresh, int type, DisplayModePtr prev)
{
	unsigned int hactive_s = xres;
	unsigned int vactive_s = yres;

	mode_ptr->HDisplay = hactive_s;
	mode_ptr->HSyncStart = hactive_s + 20;
	mode_ptr->HSyncEnd = hactive_s + 40;
	mode_ptr->HTotal = hactive_s + 80;

	mode_ptr->VDisplay = vactive_s;
	mode_ptr->VSyncStart = vactive_s + 20;
	mode_ptr->VSyncEnd = vactive_s + 40;
	mode_ptr->VTotal = vactive_s + 80;

	mode_ptr->VRefresh = vrefresh;

	mode_ptr->Clock = (int)(mode_ptr->VRefresh * mode_ptr->VTotal * mode_ptr->HTotal / 1000.0);
	mode_ptr->HSync = ((float) mode_ptr->Clock) / ((float) mode_ptr->HTotal);

	mode_ptr->Flags = V_NCSYNC | V_NVSYNC | V_NHSYNC;

	mode_ptr->type = type;

	xf86SetModeDefaultName(mode_ptr);

	mode_ptr->next = NULL;
	mode_ptr->prev = prev;

	fbdev_fill_crtc_mode(mode_ptr, xres, yres, vrefresh, type, prev);
}

void fbdev_fill_crtc_mode(DisplayModePtr mode_ptr, int xres, int yres, float vrefresh, int type, DisplayModePtr prev)
{
        mode_ptr->CrtcHDisplay = mode_ptr->HDisplay;
        mode_ptr->CrtcHSyncStart = mode_ptr->HSyncStart;
        mode_ptr->CrtcHSyncEnd = mode_ptr->HSyncEnd;
        mode_ptr->CrtcHTotal = mode_ptr->HTotal;

        mode_ptr->CrtcVDisplay = mode_ptr->VDisplay;
        mode_ptr->CrtcVSyncStart = mode_ptr->VSyncStart;
        mode_ptr->CrtcVSyncEnd = mode_ptr->VSyncEnd;
        mode_ptr->CrtcVTotal = mode_ptr->VTotal;
}

DisplayModePtr fbdev_make_mode(int xres, int yres, float vrefresh, int type, DisplayModePtr prev)
{
        DisplayModePtr mode_ptr;

        mode_ptr = xnfcalloc(1, sizeof(DisplayModeRec));

	fbdev_fill_mode(mode_ptr, xres, yres, vrefresh, type, prev);

	return mode_ptr;
}

static DisplayModePtr fbdev_lcd_output_get_modes(xf86OutputPtr output)
{
	FBDevPtr fPtr = FBDEVPTR(output->scrn);
	DisplayModePtr mode_ptr;
	ScrnInfoPtr pScrn = output->scrn;

#if 0
	unsigned int hactive_s = fPtr->fb_lcd_var.xres;
	unsigned int vactive_s = fPtr->fb_lcd_var.yres;
#endif
	float vrefresh = 60.0;

	if (pScrn->modes != NULL)
	{
		/* Use the modes supplied by the implementation if available */
		DisplayModePtr mode, first = mode = pScrn->modes;
		DisplayModePtr modeptr = NULL, modeptr_prev = NULL, modeptr_first = NULL;

		do
		{
			int type = M_T_DRIVER;
			int xres = mode->HDisplay;
			int yres = mode->VDisplay;
			float vref = mode->VRefresh;

			INFO_MSG("Adding mode: %i x %i %.0f", xres, yres, vref);

			if (modeptr_first == NULL)
			{
				modeptr_first = fbdev_make_mode(xres, yres, vref, type, NULL);
				fbdev_copy_mode(mode, modeptr_first);
				modeptr_first->type = type;
				modeptr = modeptr_first;
			}
			else
			{
				modeptr->next = fbdev_make_mode(xres, yres, vref, type, modeptr_prev);
				fbdev_copy_mode(mode, modeptr->next);
				modeptr->next->type = type;
				modeptr = modeptr->next;
			}

			modeptr_prev = modeptr;

			mode = mode->next;
		}
		while (mode != NULL && mode != first);

		return modeptr_first;
	}

	INFO_MSG("Adding fallback mode: %i x %i %.0f", fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh);
	mode_ptr = fbdev_make_mode(fPtr->fb_lcd_var.xres, fPtr->fb_lcd_var.yres, vrefresh, M_T_DRIVER, NULL);

	return mode_ptr;
}

#ifdef RANDR_GET_CRTC_INTERFACE
static xf86CrtcPtr fbdev_lcd_output_get_crtc(xf86OutputPtr output)
{
	return output->crtc;
}
#endif

static void
fbdev_lcd_output_destroy(xf86OutputPtr output)
{
	struct fbdev_lcd_output_priv *fbdev_lcd_output = output->driver_private;
	int i;

	for (i = 0; i < fbdev_lcd_output->connector->count_encoders; i++) {
		if (fbdev_lcd_output->fbdev_lcd->isDummy)
			free(fbdev_lcd_output->encoders[i]);
		else
			drmModeFreeEncoder(fbdev_lcd_output->encoders[i]);
	}

	free(fbdev_lcd_output->encoders);

	if (fbdev_lcd_output->fbdev_lcd->isDummy) {
		free(fbdev_lcd_output->connector->encoders);
		free(fbdev_lcd_output->connector);
	}
	else
		drmModeFreeConnector(fbdev_lcd_output->connector);
	free(fbdev_lcd_output);
	output->driver_private = NULL;
}

static const xf86OutputFuncsRec fbdev_lcd_output_funcs =
{
	.create_resources = NULL,
	.dpms = fbdev_lcd_output_dpms,
	.save = fbdev_lcd_output_save,
	.restore = fbdev_lcd_output_restore,
	.mode_valid = fbdev_lcd_output_mode_valid,
	.mode_fixup = fbdev_lcd_output_mode_fixup,
	.prepare = fbdev_lcd_output_prepare,
	.commit = fbdev_lcd_output_commit,
	.mode_set = fbdev_lcd_output_mode_set,
	.detect = fbdev_lcd_output_detect,
	.get_modes = fbdev_lcd_output_get_modes,
#ifdef RANDR_12_INTERFACE
	.set_property = NULL,
#endif
#ifdef RANDR_13_INTERFACE
	.get_property = NULL,
#endif
#ifdef RANDR_GET_CRTC_INTERFACE
	.get_crtc = fbdev_lcd_output_get_crtc,
#endif
	.destroy = fbdev_lcd_output_destroy,
};


static Bool FBDEV_crtc_init(ScrnInfoPtr pScrn, struct fbdev_lcd_rec *fbdev_lcd, int num)
{
	xf86CrtcPtr crtc;
	struct fbdev_lcd_crtc_private_rec *fbdev_lcd_crtc;

	crtc = xf86CrtcCreate(pScrn, &fbdev_lcd_crtc_funcs);

	if (crtc == NULL)
	{
		return FALSE;
	}

	fbdev_lcd_crtc = xnfcalloc(1, sizeof *fbdev_lcd_crtc);
	fbdev_lcd_crtc->crtc_id = fbdev_lcd->mode_res->crtcs[num];
	fbdev_lcd_crtc->fbdev_lcd = fbdev_lcd;

	INFO_MSG("Got CRTC: %d (id: %d)",
			num, fbdev_lcd_crtc->crtc_id);
	crtc->driver_private = fbdev_lcd_crtc;

	return TRUE;
}

const char *output_names[] = { "None",
		"VGA",
		"DVI-I",
		"DVI-D",
		"DVI-A",
		"Composite",
		"SVIDEO",
		"LVDS",
		"CTV",
		"DIN",
		"DP",
		"HDMI",
		"HDMI",
		"TV",
		"eDP",
};
#define NUM_OUTPUT_NAMES (sizeof(output_names) / sizeof(output_names[0]))

static void
FBDEV_output_init(ScrnInfoPtr pScrn, struct fbdev_lcd_rec *fbdev_lcd, int num)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	xf86OutputPtr output;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr *encoders = NULL;
	struct fbdev_lcd_output_priv *fbdev_lcd_output;
	char name[32];
	int i;

	TRACE_ENTER();

	if (fbdev_lcd->isDummy) {
		connector = calloc(1, sizeof *connector);
		if (!connector)
	                goto exit;

		connector->connector_id = 0;
		connector->encoder_id = 0;
		connector->connector_type = DRM_MODE_CONNECTOR_HDMIA;
		connector->connector_type_id = 1;
		connector->connection = DRM_MODE_UNKNOWNCONNECTION;
		connector->mmWidth = 0;
		connector->mmHeight = 0;
		connector->subpixel = DRM_MODE_SUBPIXEL_UNKNOWN;

		connector->count_modes = 0;
		connector->modes = NULL;

		connector->count_props = 0;
		connector->props = NULL;
		connector->prop_values = NULL;

		connector->count_encoders = 1;
		connector->encoders = calloc(1, sizeof(*connector->encoders));
		if (!connector->encoders)
			goto exit;

		connector->encoders[0] = 0;

	} else {
		connector = drmModeGetConnector(fbdev_lcd->fd, fbdev_lcd->mode_res->connectors[num]);
	}
	if (!connector)
		goto exit;

	encoders = calloc(sizeof(drmModeEncoderPtr), connector->count_encoders);
	if (!encoders)
		goto free_connector_exit;

	for (i = 0; i < connector->count_encoders; i++) {
		if (fbdev_lcd->isDummy) {
			encoders[i] = calloc(1, sizeof *encoders[i]);
			if (!encoders[i])
	                        goto free_encoders_exit;

			encoders[i]->encoder_id = 0;
			encoders[i]->encoder_type = DRM_MODE_ENCODER_TMDS;
			encoders[i]->crtc_id = 0;
			encoders[i]->possible_crtcs = (1 << 0);
			encoders[i]->possible_clones = (1 << 0);

		} else {
			encoders[i] = drmModeGetEncoder(fbdev_lcd->fd, connector->encoders[i]);
		}
		if (!encoders[i])
			goto free_encoders_exit;
	}

	if (connector->connector_type >= NUM_OUTPUT_NAMES)
		snprintf(name, 32, "Unknown%d-%d", connector->connector_type, connector->connector_type_id);
	else
		snprintf(name, 32, "%s-%d", output_names[connector->connector_type], connector->connector_type_id);

	output = xf86OutputCreate(pScrn, &fbdev_lcd_output_funcs, name);
	if (!output)
		goto free_encoders_exit;

	fbdev_lcd_output = calloc(1, sizeof *fbdev_lcd_output);
	if (!fbdev_lcd_output) {
		xf86OutputDestroy(output);
		goto free_encoders_exit;
	}

	fbdev_lcd_output->output_id = fbdev_lcd->mode_res->connectors[num];
	fbdev_lcd_output->connector = connector;
	fbdev_lcd_output->encoders = encoders;
	fbdev_lcd_output->fbdev_lcd = fbdev_lcd;

	output->mm_width = connector->mmWidth;
	output->mm_height = connector->mmHeight;
	output->driver_private = fbdev_lcd_output;

	/*
	 * Determine which crtcs are supported by all the encoders which
	 * are valid for the connector of this output.
	 */
	output->possible_crtcs = 0xffffffff;
	for (i = 0; i < connector->count_encoders; i++)
		output->possible_crtcs &= encoders[i]->possible_crtcs;
	/*
	 * output->possible_crtcs is a bitmask arranged by index of crtcs for this screen while
	 * encoders->possible_crtcs covers all crtcs supported by the drm. If we have selected
	 * one crtc per screen, it must be at index 0.
	 */
	if (fPtr->crtcNum >= 0)
		output->possible_crtcs = (output->possible_crtcs >> (fPtr->crtcNum)) & 1;

	output->possible_clones = 0; /* set after all outputs initialized */
	output->interlaceAllowed = TRUE;
	goto exit;

free_encoders_exit:
	for (i = 0; i < connector->count_encoders; i++) {
		if (fbdev_lcd->isDummy)
			free(encoders[i]);
		else
			drmModeFreeEncoder(encoders[i]);
	}

free_connector_exit:
	if (fbdev_lcd->isDummy) {
		free(connector->encoders);
		free(connector);
	}
	else
		drmModeFreeConnector(connector);

exit:
	TRACE_EXIT();
	return;

}

Bool FBDEV_lcd_init(ScrnInfoPtr pScrn)
{
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	struct fbdev_lcd_rec *fbdev_lcd;
	int i;

	TRACE_ENTER();

	fbdev_lcd = calloc(1, sizeof(*fbdev_lcd));
	if (!fbdev_lcd)
		return FALSE;

	fPtr->crtcNum = -1;
	fbdev_lcd->fd = fPtr->drmFD;

#if 0
	xf86CrtcConfigInit(pScrn, &fbdev_lcd_crtc_config_funcs);
#endif

	fbdev_lcd->isDummy = FALSE;
	fbdev_lcd->mode_res = drmModeGetResources(fbdev_lcd->fd);

	if (!fbdev_lcd->mode_res) {
		INFO_MSG("Using dummy drmMode Resources");

		fPtr->crtcNum = 0;

		//fbdev_lcd_crtc_funcs.set_mode_major = NULL;

		fbdev_lcd->isDummy = TRUE;

		fbdev_lcd->mode_res = calloc(1, sizeof(*fbdev_lcd->mode_res));
		if (!fbdev_lcd->mode_res) 
			return FALSE;

		fbdev_lcd->mode_res->count_crtcs = 1;
		fbdev_lcd->mode_res->count_connectors = 1;
		fbdev_lcd->mode_res->count_encoders = 1;
		fbdev_lcd->mode_res->count_fbs = 0;

		fbdev_lcd->mode_res->crtcs = calloc(1, sizeof(*fbdev_lcd->mode_res->crtcs));
		if (!fbdev_lcd->mode_res->crtcs)
			return FALSE;
		fbdev_lcd->mode_res->connectors = calloc(1, sizeof(*fbdev_lcd->mode_res->connectors));
		if (!fbdev_lcd->mode_res->connectors)
			return FALSE;
		fbdev_lcd->mode_res->encoders = calloc(1, sizeof(*fbdev_lcd->mode_res->encoders));
		if (!fbdev_lcd->mode_res->encoders)
			return FALSE;
		fbdev_lcd->mode_res->fbs = NULL;

		fbdev_lcd->mode_res->min_width = 640;
		fbdev_lcd->mode_res->min_height = 480;

		fbdev_lcd->mode_res->max_width = 4096;
		fbdev_lcd->mode_res->max_height = 4096;

		fbdev_lcd->mode_res->crtcs[0] = 0;
		fbdev_lcd->mode_res->connectors[0] = 0;
		fbdev_lcd->mode_res->encoders[0] = 0;
	}

	if (!fbdev_lcd->mode_res) {
		INFO_MSG("Could not get drmMode Resources");
		free(fbdev_lcd);
		return FALSE;
	} else {
		DEBUG_MSG(1, "Got KMS resources");
		DEBUG_MSG(1, "  %d connectors, %d encoders",
				fbdev_lcd->mode_res->count_connectors,
				fbdev_lcd->mode_res->count_encoders);
		DEBUG_MSG(1, "  %d crtcs, %d fbs",
				fbdev_lcd->mode_res->count_crtcs,
				fbdev_lcd->mode_res->count_fbs);
		DEBUG_MSG(1, "  %dx%d minimum resolution",
				fbdev_lcd->mode_res->min_width,
				fbdev_lcd->mode_res->min_height);
		DEBUG_MSG(1, "  %dx%d maximum resolution",
				fbdev_lcd->mode_res->max_width,
				fbdev_lcd->mode_res->max_height);
	}
	xf86CrtcSetSizeRange(pScrn, 320, 200, fbdev_lcd->mode_res->max_width,
			fbdev_lcd->mode_res->max_height);

	if (fPtr->crtcNum == -1) {
		INFO_MSG("Adding all CRTCs");
		for (i = 0; i < fbdev_lcd->mode_res->count_crtcs; i++)
			FBDEV_crtc_init(pScrn, fbdev_lcd, i);
	} else if (fPtr->crtcNum < fbdev_lcd->mode_res->count_crtcs) {
		FBDEV_crtc_init(pScrn, fbdev_lcd, fPtr->crtcNum);
	} else {
		ERROR_MSG(
				"Specified more Screens in xorg.conf than there are DRM CRTCs");
		return FALSE;
	}

	if (fPtr->crtcNum != -1) {
		if (fPtr->crtcNum <
				fbdev_lcd->mode_res->count_connectors)
			FBDEV_output_init(pScrn,
					fbdev_lcd, fPtr->crtcNum);
		else
			return FALSE;
	} else {
		for (i = 0; i < fbdev_lcd->mode_res->count_connectors; i++)
			FBDEV_output_init(pScrn, fbdev_lcd, i);
	}

#if 0
	FBDEV_clones_init(pScrn, fbdev_lcd);

	xf86InitialConfiguration(pScrn, TRUE);
#endif

	TRACE_EXIT();

	return TRUE;
}
