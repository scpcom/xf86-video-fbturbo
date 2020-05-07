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

#include <sys/ioctl.h>
#include "xorg-server.h"
#include "xf86.h"
#include "xf86Crtc.h"

#include "fb_debug.h"
#include "fbdev_priv.h"
#include "fbdev_lcd.h"

static void fbdev_lcd_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
	IGNORE(crtc);
	IGNORE(mode);
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
	.set_mode_major = NULL,
	.set_origin = fbdev_lcd_crtc_set_origin,
};

static void fbdev_lcd_output_dpms(xf86OutputPtr output, int mode)
{
	FBDevPtr fPtr = FBDEVPTR(output->scrn);

	if (mode == DPMSModeOn)
	{
		ioctl(fPtr->fb_lcd_fd, FBIOBLANK, FB_BLANK_UNBLANK);
	}
	else if (mode == DPMSModeOff)
	{
		ioctl(fPtr->fb_lcd_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
	}
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

static void fbdev_lcd_output_destroy(xf86OutputPtr output)
{
	IGNORE(output);
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


Bool FBDEV_lcd_init(ScrnInfoPtr pScrn)
{
	xf86CrtcPtr crtc;
	xf86OutputPtr output;

	crtc = xf86CrtcCreate(pScrn, &fbdev_lcd_crtc_funcs);

	if (crtc == NULL)
	{
		return FALSE;
	}

	output = xf86OutputCreate(pScrn, &fbdev_lcd_output_funcs, "LCD");

	if (output == NULL)
	{
		return FALSE;
	}

	output->possible_crtcs = (1 << 0);

	return TRUE;
}
