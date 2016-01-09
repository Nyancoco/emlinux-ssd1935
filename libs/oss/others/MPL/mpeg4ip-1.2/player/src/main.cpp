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
 */
/*
 * This is a command line based player for testing the library
 */
#include "mpeg4ip.h"
#include "codec_plugin_private.h"
#include <rtsp/rtsp_client.h>
#include "player_session.h"
#include "player_media.h"
#include "player_util.h"
#include "our_msg_queue.h"
#include "ip_port.h"
#include "media_utils.h"
#include "playlist.h"
#include "our_config_file.h"
#include <rtp/debug.h>
#include <libhttp/http.h>
#include "video.h"
#include "video_sdl.h"
#include "mpeg4ip_getopt.h"
#include "mpeg2t/mpeg2_transport.h"
#include "mpeg2ps/mpeg2_ps.h"

static int session_paused;
static int screen_size = 2;
static int fullscreen = 0;

static void media_list_query (CPlayerSession *psptr,
			      int num_video, 
			      video_query_t *vq,
			      int num_audio,
			      audio_query_t *aq)
{
  if (num_video > 0) {
    if (config.get_config_value(CONFIG_PLAY_VIDEO) != 0) {
      vq[0].enabled = 1;
    } 
  }
  if (num_audio > 0) {
    if (config.get_config_value(CONFIG_PLAY_AUDIO) != 0) {
      aq[0].enabled = 1;
    } 
  }
}

static control_callback_vft_t cc_vft = {
  media_list_query,
};
static int set_aspect_ratio(int newaspect, CPlayerSession *psptr)
{
  if (psptr != NULL) {
    switch (newaspect) {
    case 1 : 
      psptr->set_screen_size(screen_size,fullscreen,4,3);
      break;
    case 2 : 
      psptr->set_screen_size(screen_size,fullscreen,16,9);
      break;
    case 3 : 
      psptr->set_screen_size(screen_size,fullscreen,185,100);
      break;
    case 4 : 
      psptr->set_screen_size(screen_size,fullscreen,235,100);
      break;
    case 5:
      psptr->set_screen_size(screen_size, fullscreen, 1, 1);
      break;
    default: 
      psptr->set_screen_size(screen_size,fullscreen,0,0);
      newaspect = 0;
      break;
    }
    config.set_config_value(CONFIG_ASPECT_RATIO, newaspect);
  } else player_error_message("Can't set aspect ratio yet");
  return(newaspect);
}

int process_sdl_key_events (CPlayerSession *psptr,
		 				   sdl_event_msg_t *msg)
{
  int volume;
  uint64_t play_time;
  //printf("key %d mod %x\n", msg->sym, msg->mod);
  switch (msg->sym) {
  case SDLK_c:
    if ((msg->mod & KMOD_CTRL) != 0) {
      return 0;
    }
    break;
  case SDLK_x:
    if ((msg->mod & KMOD_CTRL) != 0) {
      return -1;
    }
  case SDLK_UP:
    volume = psptr->get_audio_volume();
    volume += 10;
    if (volume > 100) volume = 100;
    psptr->set_audio_volume(volume);
    config.set_config_value(CONFIG_VOLUME, volume);
    break;
  case SDLK_DOWN:
    volume = psptr->get_audio_volume();
    volume -= 10;
    if (volume < 0) volume = 0;
    psptr->set_audio_volume(volume);
    config.set_config_value(CONFIG_VOLUME, volume);
    break;
  case SDLK_SPACE:
    if (session_paused == 0) {
      psptr->pause_all_media();
      session_paused = 1;
    } else {
      if (psptr->play_all_media(FALSE) < 0) return -1;
      session_paused = 0;
    }
    break;
  case SDLK_END:
    // They want the end - just close, or go on to the next playlist.
    return 0;
  case SDLK_HOME:
    psptr->pause_all_media();
    if (psptr->play_all_media(TRUE, 0.0) < 0) return -1;
    break;
  case SDLK_RIGHT:
    if (psptr->session_is_seekable()) {
      play_time = psptr->get_playing_time();
      double ptime, maxtime;
      play_time += TO_U64(10000);
      ptime = UINT64_TO_DOUBLE(play_time);
      ptime /= 1000.0;
      maxtime = psptr->get_max_time();
      if (ptime < maxtime) {
	psptr->pause_all_media();
	if (psptr->play_all_media(FALSE, ptime) < 0) return -1;
      }
    }
    break;
  case SDLK_LEFT:
    if (psptr->session_is_seekable()) {
      play_time = psptr->get_playing_time();
      double ptime;
      psptr->pause_all_media();
      if (play_time >= TO_U64(10000)) {
	play_time -= TO_U64(10000);
	ptime = UINT64_TO_DOUBLE(play_time);
	ptime /= 1000.0;
	if (psptr->play_all_media(FALSE, ptime) < 0) return -1;
      } else {
	ptime = 0.0;
	if (psptr->play_all_media(TRUE) < 0) return -1;
      }
    }
    break;
  case SDLK_PAGEUP:
    if (screen_size < 4 && fullscreen == 0) {
      screen_size *= 2;
      psptr->set_screen_size(screen_size);
    }
    break;
  case SDLK_PAGEDOWN:
    if (screen_size > 1 && fullscreen == 0) {
      screen_size /= 2;
      psptr->set_screen_size(screen_size);
    }
    break;
  case SDLK_RETURN:
    if ((msg->mod & (KMOD_ALT | KMOD_META)) != 0) {
		fullscreen = 1;
	}
	break;
  case SDLK_ESCAPE:
	  fullscreen = 0;
	  break;

  case SDLK_0:
  case SDLK_1:
  case SDLK_2:
  case SDLK_3:
  case SDLK_4:
    if ((msg->mod & KMOD_CTRL) != 0) {
      int newaspect = msg->sym - SDLK_0;
      config.set_config_value(CONFIG_ASPECT_RATIO, newaspect);
      switch (newaspect) {
      case 1 : 
	psptr->set_screen_size(screen_size,fullscreen,4,3);
	break;
      case 2 : 
	psptr->set_screen_size(screen_size,fullscreen,16,9);
	break;
      case 3 : 
	psptr->set_screen_size(screen_size,fullscreen,185,100);
	break;
      case 4 : 
	psptr->set_screen_size(screen_size,fullscreen,235,100);
	break;
      case 5:
	psptr->set_screen_size(screen_size,fullscreen,1,1);
	break;
      default: 
	psptr->set_screen_size(screen_size,fullscreen,0,0);
	newaspect = 0;
	break;
      }
    }
    break;
  default:
    break;
  }
  return 1;

}

/*
 * Start_session will return the video persistence handle, if grab is 1.
 * set persist to the value, when you want to re-use.  Remember to delete
 */
static void *start_session (const char *name, int max_loop, int grab = 0, 
			    void *persist = NULL)
{
  char buffer[80];
  bool done = false;
  int loopcount = 0;
  CPlayerSession *psptr;

  CMsgQueue master_queue;
  SDL_sem *master_sem;

  master_sem = SDL_CreateSemaphore(0);
  snprintf(buffer, sizeof(buffer), "%s %s - %s", MPEG4IP_PACKAGE, MPEG4IP_VERSION, name);
  psptr = new CPlayerSession(&master_queue, master_sem,
			     buffer, 
			     persist);
  if (psptr == NULL) {
    return (NULL);
  }
  
  char errmsg[512];
  errmsg[0] = '\0';
  int ret = parse_name_for_session(psptr, name, errmsg, sizeof(errmsg), &cc_vft);
  if (ret < 0) {
    player_debug_message("%s %s", errmsg, name);
    delete psptr;
    return (NULL);
  }

  if (ret > 0) {
    player_debug_message("%s", errmsg);
  }

  psptr->set_up_sync_thread();
  psptr->set_screen_location(100, 100);

  fullscreen = config.get_config_value(CONFIG_FULL_SCREEN);
  set_aspect_ratio(config.get_config_value(CONFIG_ASPECT_RATIO),psptr);
  psptr->set_audio_volume(config.get_config_value(CONFIG_VOLUME));
  while (done == false) {
    if (psptr->play_all_media(TRUE) != 0) {
      delete psptr;
      return (NULL);
    }
    session_paused = 0;
    int keep_going = 0;
#ifdef NEED_SDL_VIDEO_IN_MAIN_THREAD
    int state = 0;
#endif
    do {
      CMsg *msg;
#ifdef NEED_SDL_VIDEO_IN_MAIN_THREAD
      state = psptr->sync_thread(state);
#else
      SDL_SemWaitTimeout(master_sem, 10000);
#endif
      while ((msg = master_queue.get_message()) != NULL) {
	switch (msg->get_value()) {
	case MSG_SESSION_FINISHED:
	  keep_going = 1;
	  break;
	case MSG_RECEIVED_QUIT:
	  keep_going = 1;
	  break;
	case MSG_SDL_KEY_EVENT:
	  sdl_event_msg_t *smsg;
	  uint32_t len;
	  int ret;
	  smsg = (sdl_event_msg_t *)msg->get_message(len);
	  ret = process_sdl_key_events(psptr, smsg);
	  if (ret < 0) {
	    loopcount = max_loop;
	    keep_going = 1;
	  } else if (ret == 0) {
	    keep_going = 1;
	  }
	  break;
	}
	delete msg;
      }
    } while (keep_going == 0);
    if (max_loop == -1 || loopcount != max_loop) {
      psptr->pause_all_media();
    }
    if (max_loop != -1) {
      loopcount++;
      done = loopcount >= max_loop;
    }
  }
  if (grab == 1) {
    // grab before we delete
    persist = psptr->grab_video_persistence();
  }
    
  delete psptr;
  SDL_DestroySemaphore(master_sem);
  return (persist);
}

static const char *usage= "[--help] [--version] [--loop=count] [--<config variable>=<value>] media-to-play\n"
"Use --config-vars to display configuration file variables\n";

int main (int argc, char **argv)
{

  int max_loop = 1;
  char *name;
  char buffer[FILENAME_MAX];
  char *home = getenv("HOME");
  static struct option orig_options[] = {
    { "version", 0, 0, 'v' },
    { "help", 0, 0, 'h'},
    { "config-vars", 0, 0, 'c'},
    { "loop", 1, 0, 'l'},
    { NULL, 0, 0, 0 }
  };
  bool have_unknown_opts = false;
  if (home == NULL) {
#ifdef _WIN32
	strcpy(buffer, "gmp4player_rc");
#else
    strcpy(buffer, ".gmp4player_rc");
#endif
  } else {
    strcpy(buffer, home);
    strcat(buffer, "/.gmp4player_rc");
  }
  
  config.SetDefaultFileName(buffer);
  initialize_plugins(&config);
  config.InitializeIndexes();
  opterr = 0;
  while (true) {
    int c = -1;
    int option_index = 0;
	  
    c = getopt_long_only(argc, argv, "l:hvc",
			 orig_options, &option_index);

    if (c == -1)
      break;

    switch (c) {
    case 'h':
      fprintf(stderr, "Usage: %s %s", argv[0], usage);
      exit(-1);

    case 'c':
      config.DisplayHelp();
      exit(0);
    case 'v':
      fprintf(stderr, "%s version %s\n", argv[0], MPEG4IP_VERSION);
      exit(0);
    case 'l':
      if (optarg == NULL) {
	max_loop = -1;
      } else 
	max_loop = atoi(optarg);
      break;
    case '?':
    default:
      have_unknown_opts = true;
      break;
    }
  }

  config.ReadDefaultFile();
  if (have_unknown_opts) {
    /*
     * Create an struct option that allows all the loaded configuration
     * options
     */
    struct option *long_options;
    uint32_t origo = sizeof(orig_options) / sizeof(*orig_options);
    long_options = create_long_opts_from_config(&config,
						orig_options,
						origo,
						128);
    if (long_options == NULL) {
      player_error_message("Couldn't create options");
      exit(-1);
    }
    optind = 1;
    // command line parsing
    while (true) {
      int c = -1;
      int option_index = 0;
      config_index_t ix;

      c = getopt_long_only(argc, argv, "af:hsv",
			   long_options, &option_index);

      if (c == -1)
	break;

      /*
       * We set a value of 128 to 128 + number of variables
       * we added the original options to the block, but don't
       * process them here.
       */
      if (c >= 128) {
	// we have an option from the config file
	ix = c - 128;
	player_debug_message("setting %s to %s",
		      config.GetNameFromIndex(ix),
		      optarg);
	if (config.GetTypeFromIndex(ix) == CONFIG_TYPE_BOOL &&
	    optarg == NULL) {
	  config.SetBoolValue(ix, true);
	} else
	  if (optarg == NULL) {
	    player_error_message("Missing argument with variable %s",
				 config.GetNameFromIndex(ix));
	  } else 
	    config.SetVariableFromAscii(ix, optarg);
      } else if (c == '?') {
	fprintf(stderr, "Usage: %s %s", argv[0], usage);
	exit(-1);
      }
    }
    free(long_options);
  }

  rtsp_set_error_func(library_message);
  rtsp_set_loglevel(config.get_config_value(CONFIG_RTSP_DEBUG));
  rtp_set_error_msg_func(library_message);
  rtp_set_loglevel(config.get_config_value(CONFIG_RTP_DEBUG));
  sdp_set_error_func(library_message);
  sdp_set_loglevel(config.get_config_value(CONFIG_SDP_DEBUG));
  http_set_error_func(library_message);
  http_set_loglevel(config.get_config_value(CONFIG_HTTP_DEBUG));
#ifndef _WIN32
  mpeg2t_set_error_func(library_message);
  mpeg2t_set_loglevel(config.get_config_value(CONFIG_MPEG2T_DEBUG));

  mpeg2ps_set_error_func(library_message);
  mpeg2ps_set_loglevel(config.get_config_value(CONFIG_MPEG2PS_DEBUG));
#endif
  if (config.get_config_value(CONFIG_RX_SOCKET_SIZE) != 0) {
    rtp_set_receive_buffer_default_size(config.get_config_value(CONFIG_RX_SOCKET_SIZE));
  }


  if (argc - optind <= 0) {
    fprintf(stderr, "Usage: %s %s", argv[0], usage);
    exit(-1);
  } else {
    name = argv[optind++];
  }

  const char *suffix = strrchr(name, '.');

    void *persist = NULL;
#ifdef darwin
 CSDLVideo *sdl_video = new CSDLVideo();
  sdl_video->set_image_size(176, 144, 1.0);
  sdl_video->set_screen_size(0, 2);
  persist = sdl_video;
#endif
   if ((suffix != NULL) && 
	  ((strcasecmp(suffix, ".mp4plist") == 0) ||
	   (strcasecmp(suffix, ".mxu") == 0) ||
       (strcasecmp(suffix, ".gmp4_playlist") == 0))) {
    const char *errmsg = NULL;
    CPlaylist *list = new CPlaylist(name, &errmsg);
    if (errmsg != NULL) {
      player_error_message("%s", errmsg);
      return (-1);
    }
    int loopcount = 0;
    bool done = false;
    while (done == false) {
      const char *start = list->get_first();
      do {
	if (start != NULL) {
	  if (persist == NULL) {
	    persist = start_session(start, 1, 1);
	  } else {
	    persist = start_session(start, 1, 0, persist);
	  }
	}
	start = list->get_next();
      } while (start != NULL);
      if (max_loop != -1) {
	loopcount++;
	done = loopcount >= max_loop;
      }
    }
  } else {
    start_session(name, max_loop, 0, persist);
  }
  // remove invalid global ports
  if (persist != NULL) {
    DestroyVideoPersistence(persist);
  }
  close_plugins();

  return(0); 
}  
  
