/*
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef FBTURBO_MALI_DRI2_H
#define FBTURBO_MALI_DRI2_H

#include "fbdev_bo.h"

#ifndef USE_DIX_PRIVATE
#include "uthash.h"
#endif

#define BO_MUST_BE_ODD_FRAME  1
#define BO_MUST_BE_EVEN_FRAME 2
#define BO_PASSED_ORDER_CHECK 4

/* The number of bytes randomly sampled from BO to detect its change */
#define RANDOM_SAMPLES_COUNT      64

#define DRI2_BUFFER_FB_MASK     0x02 /* FB: 1, non-FB: 0 */
#define DRI2_BUFFER_MAPPED_MASK 0x04 /* mapped: 1, not-mapped: 0 */
#define DRI2_BUFFER_REUSED_MASK 0x08 /* re-used: 1, re-created: 0 */
#define DRI2_BUFFER_AGE_MASK    0x70 /* buffer age */
#define DRI2_BUFFER_FLAG_MASK   0x7f /* dri2 buffer flag mask */

#define DRI2_BUFFER_GET_FB(flag)        ((flag) & DRI2_BUFFER_FB_MASK) ? 1 : 0
#define DRI2_BUFFER_SET_FB(flag, fb) (flag) |= (((fb) << 1) & DRI2_BUFFER_FB_MASK);
#define DRI2_BUFFER_GET_MAPPED(flag) ((flag) & DRI2_BUFFER_MAPPED_MASK) ? 1 : 0
#define DRI2_BUFFER_SET_MAPPED(flag, mapped) (flag) |= (((mapped) << 2) & DRI2_BUFFER_MAPPED_MASK);
#define DRI2_BUFFER_GET_REUSED(flag)      ((flag) & DRI2_BUFFER_REUSED_MASK) ? 1 : 0
#define DRI2_BUFFER_SET_REUSED(flag, reused) (flag) |= (((reused) << 3) & DRI2_BUFFER_REUSED_MASK);
#define DRI2_BUFFER_GET_AGE(flag) ((flag) & DRI2_BUFFER_AGE_MASK) >> 4
#define DRI2_BUFFER_SET_AGE(flag, age) (flag) |= (((age) << 4) & DRI2_BUFFER_AGE_MASK);

/* Data structure with the information about an BO */
typedef struct
{
    /* The migrated pixmap (may be NULL if it is a window) */
    PixmapPtr               pPixmap;
    int                     BackupDevKind;
    void                   *BackupDevPrivatePtr;
    int                     refcount;
#ifndef USE_DIX_PRIVATE
    UT_hash_handle          hh;
#endif

    FBTurboBOHandle         handle;
    size_t                  size;
    uint8_t                *addr;
    int                     depth;
    size_t                  width;
    size_t                  height;
    uint8_t                 bpp;
    int                     usage_hint;
    int                     extra_flags;

    FBTurboBOSecureID       secure_id;
    unsigned int            pitch;
    unsigned int            cpp;
    unsigned int            offs;

    /* This allows us to track buffer modifications */
    Bool                    has_checksum;
    uint32_t                checksum;
    uint32_t                checksum_seed;
} BOInfoRec, *BOInfoPtr;

/*
 * DRI2 related bookkeeping for windows. Because Mali r3p0 blob has
 * quirks and needs workarounds, we can't fully rely on the Xorg DRI2
 * framework. But instead have to predict what is happening on the
 * client side based on the typical blob behavior.
 *
 * The blob is doing something like this:
 *  1. Requests BackLeft DRI2 buffer (buffer A) and renders to it
 *  2. Swaps buffers
 *  3. Requests BackLeft DRI2 buffer (buffer B)
 *  4. Checks window geometry, and if it has changed - go back to step 1.
 *  5. Renders to the current back buffer (either buffer A or B)
 *  6. Swaps buffers
 *  7. Go back to step 4
 *
 * The main problem is that The Mali blob ignores DRI2-InvalidateBuffers
 * events and just uses GetGeometry polling to check whether the window
 * size has changed. Unfortunately this is racy and we may end up with a
 * size mismatch between buffer A and buffer B. This is particularly easy
 * to trigger when the window size changes exactly between steps 1 and 3.
 * See test/gles-yellow-blue-flip.c program which demonstrates this.
 */
typedef struct
{
#ifndef USE_DIX_PRIVATE
    UT_hash_handle          hh;
#endif
    DrawablePtr             pDraw;
    /* width and height must be the same for back and front buffers */
    int                     width, height;
    /* the number of back buffer requests */
    unsigned int            buf_request_cnt;
    /* the number of back/front buffer swaps */
    unsigned int            buf_swap_cnt;

    /* allocated BO (shared between back and front DRI2 buffers) */
    BOInfoPtr               bo_mem_ptr;

    /* BOs for hardware overlay and double buffering */
    BOInfoPtr               bo_back_ptr;
    BOInfoPtr               bo_front_ptr;

    /*
     * The queue for incoming BOs. We need to have it because DRI2
     * buffer requests and buffer swaps sometimes may come out of order.
     */
    BOInfoPtr               bo_queue[16];
    int                     bo_queue_head;
    int                     bo_queue_tail;

    /*
     * In the case DEBUG_WITH_RGB_PATTERN is defined, we add extra debugging
     * code for verifying that for each new frame, the background color is
     * changed as "R -> G -> B -> R -> G -> B -> ..." pattern and there are
     * no violations of this color change order. It is intended to be used
     * together with "test/gles-rgb-cycle-demo.c" program, which can generate
     * such pattern.
     */
#ifdef DEBUG_WITH_RGB_PATTERN
    char                    rgb_pattern_state;
#endif
} DRI2WindowStateRec, *DRI2WindowStatePtr;

typedef struct {
    int                     overlay_x;
    int                     overlay_y;

    WindowPtr               pOverlayWin;
    BOInfoPtr               pOverlayDirtyBO;
    Bool                    bOverlayWinEnabled;
    Bool                    bOverlayWinOverlapped;
    Bool                    bWalkingAboveOverlayWin;

    Bool                    bHardwareCursorIsInUse;
    EnableHWCursorProcPtr   EnableHWCursor;
    DisableHWCursorProcPtr  DisableHWCursor;

    DestroyWindowProcPtr    DestroyWindow;
    PostValidateTreeProcPtr PostValidateTree;
    GetImageProcPtr         GetImage;
    DestroyPixmapProcPtr    DestroyPixmap;

    /* the primary UMP secure id for accessing framebuffer */
    FBTurboBOSecureID       ump_fb_secure_id;
    /* the alternative UMP secure id used for the window resize workaround */
    FBTurboBOSecureID       ump_alternative_fb_secure_id;
    /* the UMP secure id for a dummy buffer */
    FBTurboBOSecureID       bo_null_secure_id;
    FBTurboBOHandle         bo_null_handle1;
    FBTurboBOHandle         bo_null_handle2;

    BOInfoPtr               HashPixmapToBO;
    DRI2WindowStatePtr      HashWindowState;

    int                     drm_fd;

    /* Wait for vsync when swapping DRI2 buffers */
    Bool                    bSwapbuffersWait;

    FBTurboBODevice        *bo_dev;
    FBTurboBOOps           *bo_ops;
} FBTurboMaliDRI2;

FBTurboMaliDRI2 *FBTurboMaliDRI2_Init(ScreenPtr pScreen,
                                  Bool      bUseOverlay,
                                  Bool      bSwapbuffersWait,
                                  Bool      bUseDumb);
void FBTurboMaliDRI2_Close(ScreenPtr pScreen);

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

#endif
