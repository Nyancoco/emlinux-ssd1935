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
 *		Dave Mackie		dmackie@cisco.com
 */

#include "mp4live.h"
#ifdef HAVE_FFMPEG
#include "video_ffmpeg.h"
#include "mp4av.h"
//#include "encoder-h263.h"
//#include <dsputil.h>
//#include <mpegvideo.h>

CFfmpegVideoEncoder::CFfmpegVideoEncoder()
{
  m_codec = NULL;
  m_avctx = NULL;
	m_vopBuffer = NULL;
	m_vopBufferLength = 0;
	m_YUV = NULL;
	m_push = NULL;
#ifdef OUTPUT_RAW
	m_outfile = NULL;
#endif
}

bool CFfmpegVideoEncoder::Init(CLiveConfig* pConfig, bool realTime)
{
  avcodec_init();
  avcodec_register_all();
  m_pConfig = pConfig;

  if (m_push != NULL) {
    delete m_push;
    m_push = NULL;
  }
  double rate;
  rate = TimestampTicks / pConfig->GetFloatValue(CONFIG_VIDEO_FRAME_RATE);

  m_frame_time = (Duration)rate;
  if (strcasecmp(pConfig->GetStringValue(CONFIG_VIDEO_ENCODING),
		 VIDEO_ENCODING_MPEG4) == 0) {
    m_push = new CTimestampPush(1);
    m_codec = avcodec_find_encoder(CODEC_ID_MPEG4);
    m_media_frame = MPEG4VIDEOFRAME;
#ifdef OUTPUT_RAW
    m_outfile = fopen("raw.m4v", FOPEN_WRITE_BINARY);
#endif
  } else if (strcasecmp(pConfig->GetStringValue(CONFIG_VIDEO_ENCODING),
			VIDEO_ENCODING_H263) == 0) {
    m_push = new CTimestampPush(1);
    m_codec = avcodec_find_encoder(CODEC_ID_H263);
    m_media_frame = H263VIDEOFRAME;
#ifdef OUTPUT_RAW
    m_outfile = fopen("raw.263", FOPEN_WRITE_BINARY);
#endif
  } else {
    m_push = new CTimestampPush(3);
    m_codec = avcodec_find_encoder(CODEC_ID_MPEG2VIDEO);
    m_media_frame = MPEG2VIDEOFRAME;
#ifdef OUTPUT_RAW
    m_outfile = fopen("raw.m2v", FOPEN_WRITE_BINARY);
#endif
  }

  if (m_codec == NULL) {
    error_message("Couldn't find codec");
    return false;
  }
  
  m_avctx = avcodec_alloc_context();
  m_picture = avcodec_alloc_frame();
  m_avctx->width = m_pConfig->m_videoWidth;
  m_avctx->height = m_pConfig->m_videoHeight;
  m_avctx->bit_rate = 
    m_pConfig->GetIntegerValue(CONFIG_VIDEO_BIT_RATE) * 1000;
  m_avctx->frame_rate = (int)(m_pConfig->GetFloatValue(CONFIG_VIDEO_FRAME_RATE) + 0.5);
  m_avctx->frame_rate_base = 1;
  if (pConfig->GetIntegerValue(CONFIG_VIDEO_MPEG4_PAR_WIDTH) > 0 &&
      pConfig->GetIntegerValue(CONFIG_VIDEO_MPEG4_PAR_HEIGHT) > 0) {
#ifndef HAVE_AVRATIONAL
    float asp = (float)pConfig->GetIntegerValue(CONFIG_VIDEO_MPEG4_PAR_WIDTH);
    asp /= (float)pConfig->GetIntegerValue(CONFIG_VIDEO_MPEG4_PAR_HEIGHT);
    m_avctx->aspect_ratio = asp;
#else
    AVRational asp = 
      {pConfig->GetIntegerValue(CONFIG_VIDEO_MPEG4_PAR_WIDTH),
       pConfig->GetIntegerValue(CONFIG_VIDEO_MPEG4_PAR_HEIGHT)};
    m_avctx->sample_aspect_ratio = asp;
#endif
  }
			       
  
  if (m_media_frame == MPEG2VIDEOFRAME) {
    m_avctx->gop_size = 15;
    m_avctx->b_frame_strategy = 0;
    m_avctx->max_b_frames = 2;
  } else {
    if (m_media_frame == H263VIDEOFRAME) {
      m_avctx->bit_rate = 
	m_pConfig->GetIntegerValue(CONFIG_VIDEO_BIT_RATE) * 800;
      m_avctx->bit_rate_tolerance = 
	m_pConfig->GetIntegerValue(CONFIG_VIDEO_BIT_RATE) * 200;
#if 0
      // this makes file writing difficult
      m_avctx->rtp_mode = true;
      m_avctx->rtp_payload_size = 
	m_pConfig->GetIntegerValue(CONFIG_RTP_PAYLOAD_SIZE);
#endif
    } 
    m_key_frame_count = m_avctx->gop_size = (int)
      ((m_pConfig->GetFloatValue(CONFIG_VIDEO_FRAME_RATE)+0.5)
       * m_pConfig->GetFloatValue(CONFIG_VIDEO_KEY_FRAME_INTERVAL));
    m_avctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    debug_message("key frame count is %d", m_key_frame_count);
  }
  m_count = 0;
  if (avcodec_open(m_avctx, m_codec) < 0) {
    error_message("Couldn't open codec");
    return false;
  }
  
  return true;
}

bool CFfmpegVideoEncoder::EncodeImage(
	u_int8_t* pY, u_int8_t* pU, u_int8_t* pV, 
	u_int32_t yStride, u_int32_t uvStride,
	bool wantKeyFrame, 
	Duration elapsedDuration,
	Timestamp srcFrameTimestamp)
{
  m_push->Push(srcFrameTimestamp);
	if (m_vopBuffer == NULL) {
		m_vopBuffer = (u_int8_t*)malloc(m_pConfig->m_videoMaxVopSize);
		if (m_vopBuffer == NULL) {
			return false;
		}
	}

	if (m_media_frame == H263VIDEOFRAME) {
	  m_count++;
	  if (m_count >= m_key_frame_count) {
	    wantKeyFrame = true;
	    m_count = 0;
	  }
	}
	if (wantKeyFrame) m_picture->key_frame = 1;
	else m_picture->key_frame = 0;

	m_picture->data[0] = pY;
	m_picture->data[1] = pU;
	m_picture->data[2] = pV;
	m_picture->linesize[0] = yStride;
	m_picture->linesize[1] = uvStride;
	m_picture->linesize[2] = uvStride;

	
	m_vopBufferLength = avcodec_encode_video(m_avctx, 
						 m_vopBuffer, 
						 m_pConfig->m_videoMaxVopSize, 
						 m_picture);
	//debug_message(U64" ffmpeg len %d", srcFrameTimestamp, m_vopBufferLength);
#ifdef OUTPUT_RAW
	if (m_vopBufferLength) {
	  fwrite(m_vopBuffer, m_vopBufferLength, 1, m_outfile);
	}
#endif
	//	m_avctx.frame_number++;

	return true;
}


bool CFfmpegVideoEncoder::GetEncodedImage(
	u_int8_t** ppBuffer, u_int32_t* pBufferLength,
	Timestamp *dts, Timestamp *pts)
{
  bool ret = true;
  *ppBuffer = m_vopBuffer;
  *pBufferLength = m_vopBufferLength;

  if (m_vopBufferLength != 0) {
    *dts = m_push->Pop();
    if (m_media_frame == MPEG2VIDEOFRAME) {
      // special processing for mpeg2 - the pts is not when we
      // dts
      int ret, ftype;
      ret = MP4AV_Mpeg3FindPictHdr(m_vopBuffer, m_vopBufferLength, &ftype);
      if (ret >= 0 && ftype != 3) {
	*pts = *dts + (2 * m_frame_time);
      } else {
	*pts = *dts - m_frame_time;
      }
      //error_message("dts %llu pts %llu", *dts, *pts);
    } else {
      *pts = *dts;
    }
  } else {
    *dts = *pts = 0;
    ret = false;
  }
  m_vopBuffer = NULL;
  m_vopBufferLength = 0;
  
  return ret;
}
media_free_f CFfmpegVideoEncoder::GetMediaFreeFunction(void)
{
  return NULL;
}

bool CFfmpegVideoEncoder::GetReconstructedImage(
	u_int8_t* pY, u_int8_t* pU, u_int8_t* pV)
{

#if 1
  if (m_avctx->coded_frame->linesize[0] == m_pConfig->m_videoWidth) {
	memcpy(pY, m_avctx->coded_frame->data[0],
		m_pConfig->m_ySize);
	memcpy(pU, m_avctx->coded_frame->data[1],
		m_pConfig->m_uvSize);
	memcpy(pV, m_avctx->coded_frame->data[2],
		m_pConfig->m_uvSize);
  } else {
    const uint8_t *sY, *sU, *sV;
    sY = m_avctx->coded_frame->data[0];
    sU = m_avctx->coded_frame->data[1];
    sV = m_avctx->coded_frame->data[2];
    uint16_t ix;
    for (ix = 0; ix < m_pConfig->m_videoHeight; ix++) {
      memcpy(pY, sY, m_pConfig->m_videoWidth);
      pY += m_pConfig->m_videoWidth;
      sY += m_avctx->coded_frame->linesize[0];
    }
    for (ix = 0; ix < m_pConfig->m_videoHeight / 2; ix++) {
      memcpy(pU, sU, m_pConfig->m_videoWidth / 2);
      pU += m_pConfig->m_videoWidth / 2;
      sU += m_avctx->coded_frame->linesize[1];
      memcpy(pV, sV, m_pConfig->m_videoWidth / 2);
      pV += m_pConfig->m_videoWidth / 2;
      sV += m_avctx->coded_frame->linesize[2];
    }
  }
	
	return true;
#else
	return false;
#endif
}

void CFfmpegVideoEncoder::Stop()
{
  avcodec_close(m_avctx);
  CHECK_AND_FREE(m_YUV);
  CHECK_AND_FREE(m_picture);
  CHECK_AND_FREE(m_avctx);
#ifdef OUTPUT_RAW
  if (m_outfile) {
    fclose(m_outfile);
  }
#endif
	  
}

#endif
