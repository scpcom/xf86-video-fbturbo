/*
Based on tvservice.c
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// ---- Include Files -------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <string.h>

#include "xorg-server.h"
#include "xf86.h"

#include "interface/vmcs_host/vc_tvservice.h"

#define TV_SUPPORTED_MODE_T TV_SUPPORTED_MODE_NEW_T
#define vc_tv_hdmi_get_supported_modes vc_tv_hdmi_get_supported_modes_new
#define vc_tv_hdmi_power_on_explicit vc_tv_hdmi_power_on_explicit_new

#include "fb_debug.h"
#include "fbdev_vc4.h"

// ---- Public Variables ----------------------------------------------------

// ---- Private Constants and Types -----------------------------------------

#if 0
// Logging macros (for remapping to other logging mechanisms, i.e., vcos_log)
#define LOG_ERR(  fmt, arg... )  fprintf( stderr, "[E] " fmt "\n", ##arg )
#define LOG_WARN( fmt, arg... )  fprintf( stderr, "[W] " fmt "\n", ##arg )
#define LOG_INFO( fmt, arg... )  fprintf( stderr, "[I] " fmt "\n", ##arg )
#define LOG_DBG(  fmt, arg... )  fprintf( stdout, "[D] " fmt "\n", ##arg )

// Standard output log (for printing normal information to users)
#define LOG_STD(  fmt, arg... )  fprintf( stdout, fmt "\n", ##arg )
#else
#define LOG_ERR ERROR_STR
#define LOG_WARN( fmt, arg... )  DEBUG_STR( 0, "[W] " fmt, ##arg )
#define LOG_INFO( fmt, arg... )  DEBUG_STR( 0, "[I] " fmt, ##arg )
#define LOG_DBG(  fmt, arg... )  DEBUG_STR( 1, "[D] " fmt, ##arg )

#define LOG_STD(  fmt, arg... )  DEBUG_STR( 0, fmt, ##arg )
#endif

#define status_mode(tvstate) ""

static int get_status( uint32_t *w, uint32_t *h, float *f_r )
{
   TV_DISPLAY_STATE_T tvstate;
   if( vc_tv_get_display_state( &tvstate ) == 0) {
      //The width/height parameters are in the same position in the union
      //for HDMI and SDTV
      HDMI_PROPERTY_PARAM_T property;
      property.property = HDMI_PROPERTY_PIXEL_CLOCK_TYPE;
      vc_tv_hdmi_get_property(&property);
      float frame_rate = property.param1 == HDMI_PIXEL_CLOCK_TYPE_NTSC ? tvstate.display.hdmi.frame_rate * (1000.0f/1001.0f) : tvstate.display.hdmi.frame_rate;

      if(tvstate.display.hdmi.width && tvstate.display.hdmi.height) {
         LOG_STD( "state 0x%x [%s], %ux%u @ %.2fHz, %s", tvstate.state,
                  status_mode(&tvstate),
                  tvstate.display.hdmi.width, tvstate.display.hdmi.height,
                  frame_rate,
                  tvstate.display.hdmi.scan_mode ? "interlaced" : "progressive" );

         *w = tvstate.display.hdmi.width;
         *h = tvstate.display.hdmi.height;
         *f_r = frame_rate;

         return 0;
      } else {
         LOG_STD( "state 0x%x [%s]", tvstate.state, status_mode(&tvstate));
      }
   } else {
      LOG_STD( "Error getting current display state");
   }
  return 1;
}

int vc_vchi_tv_get_status( uint32_t *w, uint32_t *h, float *f_r )
{
   int32_t ret = 0;
   VCHI_INSTANCE_T    vchi_instance;
   VCHI_CONNECTION_T *vchi_connection;

   // Initialize VCOS
   vcos_init();

   // Initialize the VCHI connection
   ret = vchi_initialise( &vchi_instance );
   if ( ret != 0 )
   {
      LOG_ERR( "Failed to initialize VCHI (ret=%d)", ret );
      goto err_out;
   }

   ret = vchi_connect( NULL, 0, vchi_instance );
   if ( ret != 0)
   {
      LOG_ERR( "Failed to create VCHI connection (ret=%d)", ret );
      goto err_out;
   }

//   LOG_INFO( "Starting tvservice" );

   // Initialize the tvservice
   vc_vchi_tv_init( vchi_instance, &vchi_connection, 1 );

   ret = get_status(w, h, f_r);

err_stop_service:
//   LOG_INFO( "Stopping tvservice" );

   // Stop the tvservice
   vc_vchi_tv_stop();

   // Disconnect the VCHI connection
   vchi_disconnect( vchi_instance );

err_out:
   return !ret;
}

