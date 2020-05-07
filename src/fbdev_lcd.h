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

#ifndef _FBDEV_LCD_H_
#define _FBDEV_LCD_H_

#define DPMSModeOn         0
#define DPMSModeStandby    1
#define DPMSModeSuspend    2
#define DPMSModeOff        3

uint32_t fbdev_lcd_vrefresh(uint32_t m_vrefresh, uint32_t m_clock,
		uint16_t m_htotal, uint16_t m_vtotal, uint16_t m_vscan,
		uint32_t m_flags);

extern void fbdev_copy_mode(DisplayModePtr source_mode_ptr, DisplayModePtr mode_ptr);
extern void fbdev_fill_mode(DisplayModePtr mode_ptr, int xres, int yres, float vrefresh, int type, DisplayModePtr prev);
extern void fbdev_fill_crtc_mode(DisplayModePtr mode_ptr, int xres, int yres, float vrefresh, int type, DisplayModePtr prev);
extern DisplayModePtr fbdev_make_mode(int xres, int yres, float vrefresh, int type, DisplayModePtr prev);

extern Bool FBDEV_lcd_init(ScrnInfoPtr pScrn);

#endif /* _FBDEV_LCD_H_ */

