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

#ifndef _FB_DEBUG_H_
#define _FB_DEBUG_H_

#ifndef FBTURBO_DEBUG
#define FBTURBO_DEBUG 1
#define FBTURBO_DEBUG_LEVEL 1
#endif

#if FBTURBO_DEBUG
#define TRACE_ENTER(str) \
	do { xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[%s:%d]: Entering\n",\
		                __FUNCTION__, __LINE__); } while (0)
#define TRACE_EXIT(str) \
	do { xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[%s:%d]: Exiting\n",\
		                __FUNCTION__, __LINE__); } while (0)
#define DEBUG_MSG(nr,fmt, ...) \
	do { if (nr <= FBTURBO_DEBUG_LEVEL) \
			xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[%s:%d] " fmt "\n",\
			           __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define INFO_MSG(fmt, ...) \
	do { xf86DrvMsg(pScrn->scrnIndex, X_INFO, fmt "\n",\
		                ##__VA_ARGS__); } while (0)
#define CONFIG_MSG(fmt, ...) \
	do { xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, fmt "\n",\
		                ##__VA_ARGS__); } while (0)
#define WARNING_MSG(fmt, ...) \
	do { xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "[%s:%d] Warning: " fmt "\n",\
		                __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define ERROR_MSG(fmt, ...) \
	do { xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "[%s:%d] Error: " fmt "\n",\
		                __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define ERROR_STR(fmt, ...) \
	do { xf86Msg(X_ERROR, "FBTURBO: Error: " fmt "\n",\
		             ##__VA_ARGS__); } while (0)
#define DEBUG_STR(nr, fmt, ...) \
	do { if (nr <= FBTURBO_DEBUG_LEVEL) \
			xf86Msg(X_WARNING, "FBTURBO: Debug: " fmt "\n",\
			        ##__VA_ARGS__); } while (0)
#else
#define TRACE_ENTER(str)
#define TRACE_EXIT(str)
#define DEBUG_MSG(nr,fmt, ...)
#define INFO_MSG(fmt, ...)
#define CONFIG_MSG(fmt, ...)
#define WARNING_MSG(fmt, ...)
#define ERROR_MSG(fmt, ...)
#define ERROR_STR(fmt, ...)
#define DEBUG_STR(nr,fmt,...)
#endif

#define IGNORE( a )  ( a = a );

#endif
