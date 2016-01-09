/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is MPEG4IP.
 * 
 * The Initial Developer of the Original Code is Cisco Systems Inc.
 * Portions created by Cisco Systems Inc. are
 * Copyright (C) Cisco Systems Inc. 2000, 2001.  All Rights Reserved.
 * 
 * Contributor(s): 
 *              Bill May        wmay@cisco.com
 *              video aspect ratio by:
 *              Peter Maersk-Moller peter@maersk-moller.net
 */
/*
 * video.cpp - provides codec to video hardware class
 */
#include <string.h>
#include "player_session.h"
#include "video_sdl.h"
#include "player_util.h"
#include "SDL_error.h"
#include "SDL_syswm.h"
#include "our_config_file.h"

//#define VIDEO_SYNC_PLAY 2
//#define VIDEO_SYNC_FILL 1
//#define SHORT_VIDEO 1
//#define SWAP_UV 1

#ifdef _WIN32
// new hwsurface method doesn't work on windows.
// If you have pre-directX 8.1 - you'll want to define OLD_SURFACE
//#define OLD_SURFACE 1
#define WM_WIN 1
#endif

#if defined (__unix__)
#define WM_X11
#endif

#ifdef _WIN32
DEFINE_MESSAGE_MACRO(video_message, "videosync")
#else
#define video_message(loglevel, fmt...) message(loglevel, "videosync", fmt)
#endif

/*
 * CSDLVideo class is the interface to SDL.  We probably want to 
 * move this to another file, to leave the ring buffer code (what is
 * CSDLVideoSync) alone
 * Having this seperate allows us to keep the video window present between
 * changing sessions.
 */
CSDLVideo::CSDLVideo(int initial_x, int initial_y)
{
  char buf[32];
  const SDL_VideoInfo *video_info;
  m_image = NULL;
  m_screen = NULL;
  m_pos_x = initial_x;
  m_pos_y = initial_y;
  m_name = NULL;
  m_pixel_width = -1;
  m_pixel_height = -1;
  m_max_width = -1;
  m_max_height = -1;
  // One time initialization stuff
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0 || !SDL_VideoDriverName(buf, 1)) {
    video_message(LOG_CRIT, "Could not init SDL video: %s", SDL_GetError());
  }
  video_info = SDL_GetVideoInfo();
  switch (video_info->vfmt->BitsPerPixel) {
  case 16:
  case 32:
    m_video_bpp = video_info->vfmt->BitsPerPixel;
    break;
  default:
    m_video_bpp = 16;
    break;
  }
  m_old_win_w = m_old_win_h = 0;
  m_old_w = m_old_h = 0;
  SDL_ShowCursor(SDL_DISABLE);

  //Query the current Window system resolution 
  SDL_SysWMinfo         info;
  SDL_VERSION(&info.version);
  if (SDL_GetWMInfo(&info)) {
#if defined(WM_X11)
  if ( info.subsystem == SDL_SYSWM_X11 ) {
        info.info.x11.lock_func();
	m_max_width = DisplayWidth(info.info.x11.display, 
                            DefaultScreen(info.info.x11.display));
	m_max_height = DisplayHeight(info.info.x11.display, 
                            DefaultScreen(info.info.x11.display));
        info.info.x11.unlock_func();
        video_message(LOG_INFO, "Max Window resolution %dx%d", m_max_width, m_max_height);
  } else {
      video_message(LOG_ERR, "Failed to get Max resolution foe window system");
  }
#elif defined (WM_WIN)
      m_max_width=GetSystemMetrics(SM_CXSCREEN);
      m_max_height=GetSystemMetrics(SM_CYSCREEN);
      video_message(LOG_INFO, "Max Window resolution %dx%d", m_max_width, m_max_height);
#endif
  }
}

/*
 * Clean up - free everything
 */
CSDLVideo::~CSDLVideo (void)
{
  CHECK_AND_FREE(m_name);
  if (m_image) {
    SDL_FreeYUVOverlay(m_image);
    m_image = NULL;
  }
  if (m_screen) {
    SDL_FreeSurface(m_screen);
    m_screen = NULL;
  }
}

void CSDLVideo::set_name (const char *name)
{
  CHECK_AND_FREE(m_name);
  m_name = strdup(name);
}

void CSDLVideo::set_image_size (unsigned int w, unsigned int h, double aspect)
{
  m_image_w = w;
  m_image_h = h;
  m_aspect_ratio = aspect;
  // don't actually do anything until set_screen_size is called
}

/*
 * Set the correct screen size.
 */
void  CSDLVideo::set_screen_size(int fullscreen, int video_scale, 
				 int pixel_width, int pixel_height,
				 int max_width, int max_height)
{
  if (pixel_width > -1) m_pixel_width = pixel_width;
  if (pixel_height > -1) m_pixel_height = pixel_height;
  if (max_width > -1) m_max_width = max_width;
  if (max_height > -1) m_max_height = max_height;

  // Check and see if we should use old values.
  if (pixel_width == -1 && m_pixel_width > -1) pixel_width = m_pixel_width;
  if (pixel_height == -1 && m_pixel_height > -1) pixel_height = m_pixel_height;
  if (max_width == -1 && m_max_width > -1) max_width = m_max_width;
  if (max_height == -1 && m_max_height > -1) max_height = m_image_h;
 
  int win_w = m_image_w * video_scale / 2;
  int win_h = m_image_h * video_scale / 2;
    
  // It is only when we have positive values larger than zero for
  // pixel sizes that we resize
  if (pixel_width > 0 && pixel_height > 0) {
 
    // Square pixels needs no resize.
    if (pixel_width != pixel_height) {
 
      // enable different handling of full screen versus non full screen
      if (fullscreen == 0) {
	if (pixel_width > pixel_height)
	  win_w = win_h * pixel_width / pixel_height;
	else 
	  win_h = win_w * pixel_height / pixel_width;
      } else {
 
	// For now we do the same as in non full screen.
	if (pixel_width > pixel_height)
	  win_w = win_h * pixel_width / pixel_height;
	else 
	  win_h = win_w * pixel_height / pixel_width;
      }
    }
  } else {
    // this case is when we set it from the aspect ratio defined.
    if (m_aspect_ratio != 0.0 && m_aspect_ratio != 1.0) {
      double win_wf = m_aspect_ratio;
      int win_wtest;
      win_wf *= win_h;
      video_message(LOG_INFO, "aspect ratio %g %g %d %d", 
		    m_aspect_ratio, win_wf, win_w, win_h);
      win_wtest = (int)win_wf;
      if (win_wtest >= win_w) {
	win_w = win_wtest;
      } else {
	video_message(LOG_ERR, "aspect ratio width less than orig");
      }
    }
  }
  if (fullscreen == 1) {
    //Suren, get the max resolution of window system and scale to that
    //--------
    if(m_max_width > 0 ) win_w = m_max_width;
    if (m_max_height > 0 ) win_h = m_max_height;
    video_message(LOG_INFO, "Setting full screen mode %dx%d", win_w, win_h);
    //--------
  }

  m_video_scale = video_scale;

#ifdef OLD_SURFACE
  int mask = SDL_SWSURFACE | SDL_ASYNCBLIT;
#else
  int mask = SDL_HWSURFACE;
#endif
  if (fullscreen != 0) {
    mask |= SDL_FULLSCREEN;
#ifdef _WIN32
    video_scale = 2;
#endif
  }

  if (win_w == m_old_win_w &&
      win_h == m_old_win_h &&
      m_image_w == m_old_w &&
      m_image_h == m_old_h &&
      m_fullscreen == fullscreen) {
    return;
  }
  video_message(LOG_DEBUG, "Setting video mode %d %d %x %g", 
		win_w, win_h, mask, m_aspect_ratio);
  m_fullscreen = fullscreen;
  if (m_image) {
    SDL_FreeYUVOverlay(m_image);
    m_image = NULL;
  }

  if (m_screen) {
    SDL_FreeSurface(m_screen);
    m_screen = NULL;
  }

  m_screen = SDL_SetVideoMode(win_w, win_h, m_video_bpp, 
			      mask);
  if (m_screen == NULL) {
    m_screen = SDL_SetVideoMode(win_w, win_h, m_video_bpp, mask);
    if (m_screen == NULL) {
      video_message(LOG_CRIT, "sdl error message is %s", SDL_GetError());
      abort();
    }
  }
  if (m_pos_x != 0 || m_pos_y != 0) {
    // we only try this the first time the video window gets initialized - 
    // not if we're resize a persistent connection
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    int ret;
    ret = SDL_GetWMInfo(&info);
    if (ret > 0) {
#ifdef unix
      if (info.subsystem == SDL_SYSWM_X11) {
	info.info.x11.lock_func();
	XMoveWindow(info.info.x11.display, info.info.x11.wmwindow, 
		    m_pos_x, m_pos_y);
	info.info.x11.unlock_func();
      }
#endif
    }
    m_pos_x = 0;
    m_pos_y = 0;
  }

  m_dstrect.x = 0;
  m_dstrect.y = 0;
  m_dstrect.w = m_screen->w;
  m_dstrect.h = m_screen->h;

#ifdef OLD_SURFACE
  if (video_scale == 4) {
    m_image = SDL_CreateYUVOverlay(m_image_w << 1, 
				   m_image_h << 1,
				   SDL_YV12_OVERLAY, 
				   m_screen);
  } else 
#endif
    {
    m_image = SDL_CreateYUVOverlay(m_image_w, 
				   m_image_h,
				   SDL_YV12_OVERLAY, 
				   m_screen);
    }
  if (m_name && strlen(m_name) != 0) {
    SDL_WM_SetCaption(m_name, NULL);
  }
  m_old_win_w = win_w;
  m_old_win_h = win_h;
  m_old_w = m_image_w;
  m_old_h = m_image_h;
}

/*
 * Display the image - copy the yuv buffers to the SDL overlay
 * height and width are set via set_image_size call
 */
void CSDLVideo::display_image (uint8_t *y, uint8_t *u, uint8_t *v)
{
  unsigned int ix;
  uint8_t *to, *from;

  if (SDL_LockYUVOverlay(m_image)) {
    video_message(LOG_ERR, "Failed to lock image");
    return;
  } 

  // Must always copy the buffer to memory.  This creates 2 copies of this
  // data (probably a total of 6 - libsock -> rtp -> decoder -> our ring ->
  // sdl -> hardware)
#ifdef OLD_SURFACE
  if (m_fullscreen == 0 && m_video_scale == 4) {
    // when scaling to 200%, don't use SDL stretch blit
    // use a smoothing (averaging) blit instead
    // we only do this for maybe windows - otherwise, let SDL do it.
#if 0
    // sorry - too many problems with this...
    FrameDoublerMmx(y, m_image->pixels[0], 
		    m_image_w, m_image_h);
    FrameDoublerMmx(v, m_image->pixels[1], 
		    m_image_w >> 1, m_image_h >> 1);
    FrameDoublerMmx(u, m_image->pixels[2], 
		    m_image_w >> 1, m_image_h >> 1);
#else
    FrameDoubler(y, m_image->pixels[0], 
		 m_image_w, m_image_h, m_image->pitches[0]);
    FrameDoubler(v, m_image->pixels[1], 
		 m_image_w >> 1, m_image_h >> 1, m_image->pitches[1]);
    FrameDoubler(u, m_image->pixels[2], 
		 m_image_w >> 1, m_image_h >> 1, m_image->pitches[2]);
#endif
  } else 
#endif
    {
      // let SDL blit, either 1:1 for 100% or decimating by 2:1 for 50%
      uint32_t bufsize = m_image_w * m_image_h * sizeof(uint8_t);
      unsigned int width = m_image_w, height = m_image_h;
      if (width != m_image->pitches[0]) {
	// The width is not equal to the size in the SDL buffers - 
	// we need to copy a row at a time
	to = (uint8_t *)m_image->pixels[0];
	from = y;
	for (ix = 0; ix < height; ix++) {
	  memcpy(to, from, width);
	  to += m_image->pitches[0];
	  from += width;
	}
      } else {
	// Copy entire Y frame
	memcpy(m_image->pixels[0], 
	       y,
	       bufsize);
      }
      // We reduce the sizes for U and V planes (they are 1/4 the size)
      bufsize /= 4;
      width /= 2;
      height /= 2;
#ifdef SWAP_UV
#define V 2
#define U 1
#else
#define V 1
#define U 2
#endif
      // Copy the U and V - same comments as above
      if (width != m_image->pitches[V]) {
	to = (uint8_t *)m_image->pixels[V];
	from = v;
	for (ix = 0; ix < height; ix++) {
	  memcpy(to, from, width);
	  to += m_image->pitches[V];
	  from += width;
	}
      } else {
	memcpy(m_image->pixels[V], 
	       v,
	       bufsize);
      }
      if (width != m_image->pitches[U]) {
	to = (uint8_t *)m_image->pixels[U];
	from = u;
	for (ix = 0; ix < height; ix++) {
	  memcpy(to, from, width);
	  to += m_image->pitches[U];
	  from += width;
	}
      } else {
	memcpy(m_image->pixels[U], 
	       u,
	       bufsize);
      }
      
    }

  // Actually display the video
  int rval = SDL_DisplayYUVOverlay(m_image, &m_dstrect);
#ifndef darwin
#define CORRECT_RETURN 0
#else
#define CORRECT_RETURN 1
#endif
  if (rval != CORRECT_RETURN) {
    video_message(LOG_ERR, "Return from display is %d", rval);
  }
  // and unlock it.
  SDL_UnlockYUVOverlay(m_image);
}

#define BLANK_Y (0)
#define BLANK_U (0x80)
#define BLANK_V (0x80)
void CSDLVideo::blank_image (void)
{
  unsigned int ix;
  uint8_t *to;
  if (SDL_LockYUVOverlay(m_image)) {
    video_message(LOG_ERR, "Failed to lock image");
    return;
  } 
  uint32_t bufsize = m_image_w * m_image_h * sizeof(uint8_t);
  unsigned int width = m_image_w, height = m_image_h;
  if (width != m_image->pitches[0]) {
    // The width is not equal to the size in the SDL buffers - 
    // we need to copy a row at a time
    to = (uint8_t *)m_image->pixels[0];
    for (ix = 0; ix < height; ix++) {
      memset(to, BLANK_Y, width);
      to += m_image->pitches[0];
    }
  } else {
    // Copy entire Y frame
    memset(m_image->pixels[0], 
	   BLANK_Y,
	   bufsize);
  }
      
  // We reduce the sizes for U and V planes (they are 1/4 the size)
  bufsize /= 4;
  width /= 2;
  height /= 2;
#ifdef SWAP_UV
#define V 2
#define U 1
#else
#define V 1
#define U 2
#endif
  // Copy the U and V - same comments as above
  if (width != m_image->pitches[V]) {
    to = (uint8_t *)m_image->pixels[V];
    for (ix = 0; ix < height; ix++) {
      memset(to, BLANK_U, width);
      to += m_image->pitches[V];
    }
  } else {
    memset(m_image->pixels[V], 
	   BLANK_U,
	   bufsize);
  }
  if (width != m_image->pitches[U]) {
    to = (uint8_t *)m_image->pixels[U];
    for (ix = 0; ix < height; ix++) {
      memset(to, BLANK_V, width);
      to += m_image->pitches[U];
    }
  } else {
    memset(m_image->pixels[U], 
	   BLANK_V,
	   bufsize);
  }
      
  SDL_DisplayYUVOverlay(m_image, &m_dstrect);
  SDL_UnlockYUVOverlay(m_image);
}
/*
 * CSDLVideoSync - actually a ring buffer for YUV frames - probably
 * can pull it out if you want to replace SDL
 */

CSDLVideoSync::CSDLVideoSync (CPlayerSession *psptr,
			      void *video_persistence) : 
  CVideoSync(psptr, video_persistence)
{
  m_sdl_video = NULL;
  m_video_initialized = 0;
  m_config_set = 0;
  m_have_data = 0;
  for (int ix = 0; ix < MAX_VIDEO_BUFFERS; ix++) {
    m_y_buffer[ix] = NULL;
    m_u_buffer[ix] = NULL;
    m_v_buffer[ix] = NULL;
    m_buffer_filled[ix] = 0;
  }
  m_play_index = m_fill_index = 0;
  m_decode_waiting = 0;
  m_dont_fill = 0;
  m_paused = 1;
  m_behind_frames = 0;
  m_total_frames = 0;
  m_behind_time = 0;
  m_behind_time_max = 0;
  m_skipped_render = 0;
  m_video_scale = 2;
  m_last_filled_time = TO_U64(0x7fffffffffffffff);
  m_msec_per_frame = 100;
  m_consec_skipped = 0;
  m_fullscreen = 0;
  m_filled_frames = 0;
  video_message(LOG_DEBUG, "vp is %p", video_persistence);
#ifdef WRITE_YUV
  m_outfile = fopen("raw.yuv", FOPEN_WRITE_BINARY);
#endif
}

CSDLVideoSync::~CSDLVideoSync (void)
{
  if ((m_grabbed_video_persistence == 0) &&
      (m_sdl_video != NULL)){
    if (m_fullscreen != 0) {
      m_sdl_video->set_screen_size(0, 2);
    }
    video_message(LOG_ERR, "deleteing video sdl");
    delete m_sdl_video;
  } else {
    if (m_sdl_video != NULL) 
      m_sdl_video->blank_image();
  }
  for (int ix = 0; ix < MAX_VIDEO_BUFFERS; ix++) {
    if (m_y_buffer[ix] != NULL) {
      free(m_y_buffer[ix]);
      m_y_buffer[ix] = NULL;
    }
    if (m_u_buffer[ix] != NULL) {
      free(m_u_buffer[ix]);
      m_u_buffer[ix] = NULL;
    }
    if (m_v_buffer[ix] != NULL) {
      free(m_v_buffer[ix]);
      m_v_buffer[ix] = NULL;
    }
  }
#ifdef WRITE_YUV
  if (m_outfile != NULL) {
    fclose(m_outfile);
  }
#endif
}

/*
 * CSDLVideoSync::config - routine for the codec to call to set up the
 * width, height and frame_rate of the video
 */
void CSDLVideoSync::config (int w, int h, double aspect_ratio)
{
  m_width = w;
  m_height = h;
  m_aspect_ratio = aspect_ratio;
  for (int ix = 0; ix < MAX_VIDEO_BUFFERS; ix++) {
    m_y_buffer[ix] = (uint8_t *)malloc(w * h * sizeof(uint8_t));
    m_u_buffer[ix] = (uint8_t *)malloc(w/2 * h/2 * sizeof(uint8_t));
    m_v_buffer[ix] = (uint8_t *)malloc(w/2 * h/2 * sizeof(uint8_t));
    m_buffer_filled[ix] = 0;
  }
  m_config_set = 1;
}

/*
 * CSDLVideoSync::initialize_video - Called from sync task to initialize
 * the video window
 */  
int CSDLVideoSync::initialize_video (const char *name, int x, int y)
{
  if (m_video_initialized == 0) {
    if (m_config_set) {
      if (m_sdl_video == NULL) {
	if (m_video_persistence != NULL) {
	  m_sdl_video = (CSDLVideo *)m_video_persistence;
	} else {
	  m_sdl_video = new CSDLVideo(x, y);
	  m_video_persistence = m_sdl_video;
	}
      }
      m_sdl_video->set_name(name);
      m_sdl_video->set_image_size(m_width, m_height, m_aspect_ratio);
      m_sdl_video->set_screen_size(m_fullscreen, m_video_scale);
      return (1);
    } else {
      return (0);
    }
  }
  return (1);
}

/*
 * CSDLVideoSync::is_video_ready - called from sync task to determine if
 * we have sufficient number of buffers stored up to start displaying
 */
int CSDLVideoSync::is_video_ready (uint64_t &disptime)
{
  disptime = m_play_this_at[m_play_index];
  if (m_dont_fill) {
    return 0;
  }
#ifdef SHORT_VIDEO
  return (m_buffer_filled[m_play_index] == 1);
#else
  return (m_buffer_filled[(m_play_index + 2*MAX_VIDEO_BUFFERS/3) % MAX_VIDEO_BUFFERS] == 1);
#endif
}

void CSDLVideoSync::play_video (void) 
{

}

/*
 * CSDLVideoSync::play_video_at - called from sync task to show the next
 * video frame (if the timing is right
 */
bool CSDLVideoSync::play_video_at (uint64_t current_time, 
				   bool have_audio_resync,
				   uint64_t &next_time,
				   bool &have_eof)
{
  uint64_t play_this_at = 0;
  bool done = false;
  bool used_frame = false;
#ifdef VIDEO_SYNC_FILL
  uint32_t temp = 0;
#endif
  bool have_next_time = true;

  while (done == false) {
    /*
     * m_buffer_filled is ring buffer of YUV data from decoders, with
     * timestamps.
     * If the next buffer is not filled, indicate that, as well
     */
    if (m_buffer_filled[m_play_index] == 0) {
      /*
       * If we have end of file, indicate it
       */
      if (m_eof_found != 0) {
	have_eof = true;
      }
      done = true;
      have_next_time = false;
      continue;
    }
  
    /*
     * we have a buffer.  If it is in the future, don't play it now
     */
    play_this_at = m_play_this_at[m_play_index];
    if (play_this_at > current_time) {
      have_next_time = true;
      done = true;
      continue;
    }
#if VIDEO_SYNC_PLAY
    video_message(LOG_DEBUG, "play "U64" at "U64 " %d "U64, play_this_at, current_time,
		  m_play_index, m_msec_per_frame);
#endif

    /*
     * If we're behind - see if we should just skip it
     */
    if (play_this_at < current_time) {
      uint64_t behind = current_time - play_this_at;
      if (have_audio_resync && behind > TO_U64(1000)) {
	have_next_time = true;
	done = true;
	continue;
      }
      m_behind_frames++;
      m_behind_time += behind;
      if (behind > m_behind_time_max) m_behind_time_max = behind;
#if 0
      if ((m_behind_frames % 64) == 0) {
	video_message(LOG_DEBUG, "behind "U64" avg "U64" max "U64,
		      behind, m_behind_time / m_behind_frames,
		      m_behind_time_max);
      }
#endif
    }
    m_paused = 0;
    /*
     * If we're within 1/2 of the frame time, go ahead and display
     * this frame
     */
    if ((play_this_at + m_msec_per_frame) > current_time) {
      m_consec_skipped = 0;
      m_sdl_video->display_image(m_y_buffer[m_play_index],
				 m_u_buffer[m_play_index],
				 m_v_buffer[m_play_index]);
    } else {
#if 0
      video_message(LOG_DEBUG, "Video lagging current time "U64" "U64" "U64, 
		    play_this_at, current_time, m_msec_per_frame);
#endif
      /*
       * Else - we're lagging - just skip and hope we catch up...
       */
      m_skipped_render++;
      m_consec_skipped++;
    }
    /*
     * Advance the ring buffer, indicating that the decoder can fill this
     * buffer.
     */
    m_buffer_filled[m_play_index] = 0;
#ifdef VIDEO_SYNC_FILL
    temp = m_play_index;
#endif
    increment_play_index();
    used_frame = true;
  }
  if (used_frame) {
    notify_decode_thread();
  }
  next_time = play_this_at;
  return have_next_time;
}

void CSDLVideoSync::increment_play_index (void)
{
    m_play_index++;
    m_play_index %= MAX_VIDEO_BUFFERS;
    m_total_frames++;
}

void CSDLVideoSync::notify_decode_thread (void)
{
  if (m_decode_waiting) {
    // If the decode thread is waiting, signal it.
    m_decode_waiting = 0;
    SDL_SemPost(m_decode_sem);
  }
}

void CSDLVideoSync::drop_next_frame (void) 
{
  increment_play_index();
  notify_decode_thread();
  m_skipped_render++;
}

/*
 * get_video_buffer - give the decoder direct access to the YUV
 * ring buffers, so it can write into that memory
 */
int CSDLVideoSync::get_video_buffer(uint8_t **y,
				    uint8_t **u,
				    uint8_t **v)
{
  
  if (m_dont_fill != 0) 
    return (0);

  if (m_buffer_filled[m_fill_index] != 0) {
    // We don't have a buffer - indicate this, and wait for the sync thread
    // to signal us.
    m_decode_waiting = 1;
    SDL_SemWait(m_decode_sem);
    if (m_dont_fill != 0)
      return 0;
    if (m_buffer_filled[m_fill_index] != 0)
      return 0;
  }
  *y = m_y_buffer[m_fill_index];
  *u = m_u_buffer[m_fill_index];
  *v = m_v_buffer[m_fill_index];
  return (1);
}

/*
 * filled_video_buffers - API routine called when decoder has filled a 
 * buffer after it called get_video_buffer
 */
void CSDLVideoSync::filled_video_buffers (uint64_t time)
{
  int ix;
  if (m_dont_fill == 1)
    return;
  m_play_this_at[m_fill_index] = time;
  m_buffer_filled[m_fill_index] = 1;
  ix = m_fill_index;
#ifdef WRITE_YUV
  fwrite(m_y_buffer[ix], m_width * m_height, 1, m_outfile);
  fwrite(m_u_buffer[ix], (m_width * m_height) / 4, 1, m_outfile);
  fwrite(m_v_buffer[ix], (m_width * m_height) / 4, 1, m_outfile);
#endif
  m_fill_index++;
  m_fill_index %= MAX_VIDEO_BUFFERS;
  m_filled_frames++;
  //  if (m_msec_per_frame == 0 && m_last_filled_time != MAX_UINT64) {
  uint64_t temp;
  temp = time - m_last_filled_time;
  if (temp != 0) {
    if (temp < m_msec_per_frame) {
      m_msec_per_frame = temp;
#ifdef VIDEO_SYNC_PLAY
      video_message(LOG_DEBUG, "msec per frame "U64, m_msec_per_frame);
#endif
    }
  }
  m_last_filled_time = time;
  m_psptr->wake_sync_thread();
#ifdef VIDEO_SYNC_FILL
  video_message(LOG_DEBUG, "Filled "U64" %d", time, ix);
#endif
}

/*
 * CSDLVideoSync::set_video_frame - called from decoder to indicate a new
 * frame is ready.
 * Inputs - y - pointer to y buffer - should point to first byte to copy
 *          u - pointer to u buffer
 *          v - pointer to v buffer
 *          pixelw_y - width of row in y buffer (may be larger than width
 *                   set up above.
 *          pixelw_uv - width of row in u or v buffer.
 *          time - time to display
 *          current_time - current time we're displaying - this allows the
 *            codec to intelligently drop frames if it's falling behind.
 */
void CSDLVideoSync::set_video_frame(const uint8_t *y, 
				    const uint8_t*u, 
				    const uint8_t *v,
				    int pixelw_y, 
				    int pixelw_uv, 
				    uint64_t time)
{
  uint8_t *dst;
  const uint8_t *src;
  unsigned int ix;

  if (m_dont_fill != 0) {
#ifdef VIDEO_SYNC_FILL
    video_message(LOG_DEBUG, "Don't fill in video sync");
#endif
    return;
  }

  /*
   * Do we have a buffer ?  If not, indicate that we're waiting, and wait
   */
  if (m_buffer_filled[m_fill_index] != 0) {
    m_decode_waiting = 1;
    SDL_SemWait(m_decode_sem);
    if (m_buffer_filled[m_fill_index] != 0) {
#ifdef VIDEO_SYNC_FILL
      video_message(LOG_DEBUG, "Wait but filled %d", m_fill_index);
#endif
      return;
    }
  }  

  /*
   * copy the relevant data to the local buffers
   */
  m_play_this_at[m_fill_index] = time;

  src = y;
  dst = m_y_buffer[m_fill_index];
  for (ix = 0; ix < m_height; ix++) {
    memcpy(dst, src, m_width);
    dst += m_width;
    src += pixelw_y;
  }
  src = u;
  dst = m_u_buffer[m_fill_index];
  unsigned int uvheight = m_height/2;
  unsigned int uvwidth = m_width/2;
  for (ix = 0; ix < uvheight; ix++) {
    memcpy(dst, src, uvwidth);
    dst += uvwidth;
    src += pixelw_uv;
  }

  src = v;
  dst = m_v_buffer[m_fill_index];
  for (ix = 0; ix < uvheight; ix++) {
    memcpy(dst, src, uvwidth);
    dst += uvwidth;
    src += pixelw_uv;
  }
  /*
   * advance the buffer, and post to the sync task
   */
  m_buffer_filled[m_fill_index] = 1;
  ix = m_fill_index;
#ifdef WRITE_YUV
  fwrite(m_y_buffer[ix], m_width * m_height, 1, m_outfile);
  fwrite(m_u_buffer[ix], (m_width * m_height) / 4, 1, m_outfile);
  fwrite(m_v_buffer[ix], (m_width * m_height) / 4, 1, m_outfile);
#endif
  m_fill_index++;
  m_fill_index %= MAX_VIDEO_BUFFERS;
  m_filled_frames++;
  uint64_t temp;
  temp = time - m_last_filled_time;
    if (temp > 0 && temp < m_msec_per_frame) {
      m_msec_per_frame = temp;
#ifdef VIDEO_SYNC_PLAY
      video_message(LOG_DEBUG, "msec per frame "U64, m_msec_per_frame);
#endif
    }
  m_last_filled_time = time;
  m_psptr->wake_sync_thread();
#ifdef VIDEO_SYNC_FILL
  video_message(LOG_DEBUG, "filled "U64" %d", time, ix);
#endif
  return;
}

// called from sync thread.  Don't call on play, or m_dont_fill race
// condition may occur.
void CSDLVideoSync::flush_sync_buffers (void)
{
  // Just restart decode thread if waiting...
  m_dont_fill = 1;
  m_eof_found = 0;
  m_paused = 1;
  if (m_decode_waiting) {
    SDL_SemPost(m_decode_sem);
    // start debug
  }
}

// called from decode thread on both stop/start.
void CSDLVideoSync::flush_decode_buffers (void)
{
  for (int ix = 0; ix < MAX_VIDEO_BUFFERS; ix++) {
    m_buffer_filled[ix] = 0;
  }

  m_fill_index = m_play_index = 0;
  m_dont_fill = 0;
}

void CSDLVideoSync::set_screen_size (int scaletimes2)
{
  m_video_scale = scaletimes2;
}

void CSDLVideoSync::set_fullscreen (int fullscreen)
{
  m_fullscreen = fullscreen;
}

void CSDLVideoSync::do_video_resize (int pixel_width, 
				     int pixel_height, 
				     int max_width, 
				     int max_height)
{

  m_sdl_video->set_screen_size(m_fullscreen, m_video_scale, 
			       pixel_width, pixel_height,
			       max_width, max_height);
}

static void c_video_configure (void *ifptr,
			       int w,
			       int h,
			       int format,
			       double aspect_ratio)
{
  // asdf - ignore format for now
  ((CSDLVideoSync *)ifptr)->config(w, h, aspect_ratio);
}

static int c_video_get_buffer (void *ifptr, 
			       uint8_t **y,
			       uint8_t **u,
			       uint8_t **v)
{
  return (((CSDLVideoSync *)ifptr)->get_video_buffer(y, u, v));
}

static void c_video_filled_buffer(void *ifptr, uint64_t time)
{
  ((CSDLVideoSync *)ifptr)->filled_video_buffers(time);
}

static void c_video_have_frame (void *ifptr,
			       const uint8_t *y,
			       const uint8_t *u,
			       const uint8_t *v,
			       int m_pixelw_y,
			       int m_pixelw_uv,
			       uint64_t time)
{
  CSDLVideoSync *foo = (CSDLVideoSync *)ifptr;

  foo->set_video_frame(y, 
		       u, 
		       v, 
		       m_pixelw_y,
		       m_pixelw_uv,
		       time);
}

static video_vft_t video_vft = 
{
  message,
  c_video_configure,
  c_video_get_buffer,
  c_video_filled_buffer,
  c_video_have_frame,
  NULL,
};

video_vft_t *get_video_vft (void)
{
  video_vft.pConfig = &config;
  return (&video_vft);
}

CVideoSync *create_video_sync (CPlayerSession *psptr) 
{
  return new CSDLVideoSync(psptr, psptr->get_video_persistence());
}

void DestroyVideoPersistence (void *persist)
{
  CSDLVideo *temp = (CSDLVideo *)persist;
  delete temp;
  SDL_Quit();
}


