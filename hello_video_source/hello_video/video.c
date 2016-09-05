/*
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

// Video deocode demo using OpenMAX IL though the ilcient helper library

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"

int input_timeout (int filedes, unsigned int useconds) {
  fd_set set;
  struct timeval timeout;
  /* Initialize the file descriptor set. */
  FD_ZERO (&set);
  FD_SET (filedes, &set);

  /* Initialize the timeout data structure. */
  timeout.tv_sec = 0;
  timeout.tv_usec = useconds;

  /* select returns 0 if timeout, 1 if input available, -1 if error. */
  return select(FD_SETSIZE, &set, NULL, NULL, &timeout);
}

int readStdin( char *datBuffer, int maxSize){
    int retVal = -1;
    char *pos;
    retVal = read(STDIN_FILENO, datBuffer, maxSize);
    if( retVal>0 && retVal<maxSize ){
        datBuffer[retVal] = '\0';
        if ((pos=strchr(datBuffer, '\n')) != NULL)
            *pos = '\0';
    }
    return( retVal );
}

static int video_decode_test(char *filename, int loop, int layer, OMX_DISPLAYRECTTYPE dest_rect)
{
   OMX_VIDEO_PARAM_PORTFORMATTYPE format;
   OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
   COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
   COMPONENT_T *list[5];
   TUNNEL_T tunnel[4];
   ILCLIENT_T *client;
   FILE *in;
   int status = 0;
   unsigned int data_len = 0;

   memset(list, 0, sizeof(list));
   memset(tunnel, 0, sizeof(tunnel));

   if((in = fopen(filename, "rb")) == NULL)
      return -2;

   if((client = ilclient_init()) == NULL)
   {
      fclose(in);
      return -3;
   }

   if(OMX_Init() != OMX_ErrorNone)
   {
      ilclient_destroy(client);
      fclose(in);
      return -4;
   }

   // create video_decode
   if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
      status = -14;
   list[0] = video_decode;

   // create video_render
   if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[1] = video_render;

   // create clock
   if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[2] = clock;

   memset(&cstate, 0, sizeof(cstate));
   cstate.nSize = sizeof(cstate);
   cstate.nVersion.nVersion = OMX_VERSION;
   cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
   cstate.nWaitMask = 1;
   if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
      status = -13;

   // create video_scheduler
   if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[3] = video_scheduler;


//--------------------------------
OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
   memset(&configDisplay, 0, sizeof configDisplay);
   configDisplay.nSize = sizeof configDisplay;
   configDisplay.nVersion.nVersion = OMX_VERSION;
   configDisplay.nPortIndex = 90;

   configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);
   configDisplay.num = 0;
   configDisplay.layer = layer;
   configDisplay.transform = 0;
   
//   printf("layer %d\n", configDisplay.layer);

   if (dest_rect.x_offset || dest_rect.y_offset || dest_rect.width || dest_rect.height)
   {
     configDisplay.set = (OMX_DISPLAYSETTYPE)(configDisplay.set|OMX_DISPLAY_SET_DEST_RECT|OMX_DISPLAY_SET_FULLSCREEN|OMX_DISPLAY_SET_NOASPECT);
     configDisplay.dest_rect = dest_rect;
   }

   if (OMX_SetConfig(ILC_GET_HANDLE(video_render), OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
     status = -15;
//--------------------------------



   set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
   set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);
   set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

   // setup clock tunnel first
   if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
      status = -15;
   else
      ilclient_change_component_state(clock, OMX_StateExecuting);

   if(status == 0)
      ilclient_change_component_state(video_decode, OMX_StateIdle);

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = 130;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;

   if(status == 0 &&
      OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
      ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0)
   {
      OMX_BUFFERHEADERTYPE *buf;
      int port_settings_changed = 0;
      int first_packet = 1;

      ilclient_change_component_state(video_decode, OMX_StateExecuting);

      while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL) {
//----------------------------------------------------------------------
// Switch files on the fly  (Super dirty hack)
//----------------------------------------------------------------------
         int retVal = -1;
         char datBuffer[256];
         retVal = input_timeout(STDIN_FILENO, 10000);
         if( retVal > 0 ){
             readStdin( datBuffer, 255 );
             fflush( STDIN_FILENO );
//             fprintf(stdout, "[%s]", datBuffer );

               OMX_SendCommand(ILC_GET_HANDLE(video_decode),OMX_CommandFlush,130,NULL);
               OMX_SendCommand(ILC_GET_HANDLE(video_decode),OMX_CommandFlush,131,NULL);
               ilclient_wait_for_event(video_decode, OMX_EventCmdComplete, OMX_CommandFlush, 0, 130, 0, ILCLIENT_PORT_FLUSH, -1);
               ilclient_wait_for_event(video_decode, OMX_EventCmdComplete, OMX_CommandFlush, 0, 131, 0, ILCLIENT_PORT_FLUSH, -1);
               
               data_len=0;       
               
               memset(&cstate, 0, sizeof(cstate));
               cstate.nSize = sizeof(cstate);
               cstate.nVersion.nVersion = OMX_VERSION;
               cstate.eState = OMX_TIME_ClockStateStopped;
               cstate.nWaitMask = 1;
               OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate);
               
               memset(&cstate, 0, sizeof(cstate));
               cstate.nSize = sizeof(cstate);
               cstate.nVersion.nVersion = OMX_VERSION;
               cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
               cstate.nWaitMask = 1;
               OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate);
               
               ilclient_change_component_state(clock, OMX_StateExecuting);
               first_packet = 1;

             fclose(in);
             if((in = fopen(datBuffer, "rb")) == NULL){
//                 fprintf (stdout, "Can't open file\n");
                 return -2;
             }
//            fprintf (stdout, " OK\n");
//            fflush(stdout);
         }         
     
         // feed data and wait until we get port settings changed
         unsigned char *dest = buf->pBuffer;

         data_len += fread(dest, 1, buf->nAllocLen-data_len, in);

         if(port_settings_changed == 0 &&
            ((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
             (data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
                                                       ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
         {
            port_settings_changed = 1;

            if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
            {
               status = -7;
               break;
            }

            ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

            // now setup tunnel to video_render
            if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
            {
               status = -12;
               break;
            }

            ilclient_change_component_state(video_render, OMX_StateExecuting);
         }

//----------------------------------------------------------------------
// Loop files if it runs out
//----------------------------------------------------------------------         
         if(!data_len) {
            // Finished reading the file, either loop or exit.
            if (loop) {
               fseek(in, 0, SEEK_SET);
            } else {
               break;
            }
         }

         buf->nFilledLen = data_len;
         data_len = 0;

         buf->nOffset = 0;
         if(first_packet)
         {
            buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
            first_packet = 0;
         }
         else
            buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

         if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
         {
            status = -6;
            break;
         }
      }

      buf->nFilledLen = 0;
      buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

      if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
         status = -20;

      // wait for EOS from render
      ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
                              ILCLIENT_BUFFER_FLAG_EOS, 10000);

      // need to flush the renderer to allow video_decode to disable its input port
      ilclient_flush_tunnels(tunnel, 0);

      ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
   }

   fclose(in);

   ilclient_disable_tunnel(tunnel);
   ilclient_disable_tunnel(tunnel+1);
   ilclient_disable_tunnel(tunnel+2);
   ilclient_teardown_tunnels(tunnel);

   ilclient_state_transition(list, OMX_StateIdle);
   ilclient_state_transition(list, OMX_StateLoaded);

   ilclient_cleanup_components(list);

   OMX_Deinit();

   ilclient_destroy(client);
   return status;
}

void error_usage(char* name) {
   printf("Usage: %s [loop] [layer] [x y w h] fname.h264\n", name);
   exit(1);
}

int main (int argc, char **argv)
{
   //argv[0] = own filename
   //argv[1] = loop 1/0
   //argv[2] = layer
   //argv[3] = x_offset
   //argv[4] = y_offset
   //argv[5] = width
   //argv[6] = height
   //argv[7] = video filename    
   int loop = 0;
   int layer = (1<<15)-1;
   OMX_DISPLAYRECTTYPE dest_rect = {0};
   if (argc == 2) {

   } else if (argc == 3) {
      loop = atoi(argv[1]);
   } else if (argc == 4) {
      loop = atoi(argv[1]);
      layer = atoi(argv[2]);
   } else if (argc == 8) {
      loop = atoi(argv[1]);
      layer = atoi(argv[2]);
      dest_rect.x_offset = atoi(argv[3]), dest_rect.y_offset = atoi(argv[4]), dest_rect.width = atoi(argv[5]), dest_rect.height = atoi(argv[6]);
   } else {
        error_usage(argv[0]);
   }
   bcm_host_init();
   return video_decode_test(argv[argc-1], loop, layer, dest_rect);
}


