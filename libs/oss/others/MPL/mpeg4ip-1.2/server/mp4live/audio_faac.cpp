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
#include "audio_faac.h"
#include "mp4.h"
#include "mp4av.h"

static const u_int32_t samplingRateAllValues[] = {
  7350, 8000, 11025, 12000, 16000, 22050, 
  24000, 32000, 44100, 48000, 64000, 88200, 96000
};

static const u_int32_t bitRateAllValues[] = {
  8000, 16000, 24000, 32000, 40000, 48000, 
  56000, 64000, 80000, 96000, 112000, 128000, 
  144000, 160000, 192000, 224000, 256000, 320000
};

static uint32_t *faac_bitrates_for_samplerate (uint32_t samplerate, 
					       uint8_t chans, 
					       uint32_t *ret_size)
{
  uint32_t *ret = (uint32_t *)malloc(sizeof(bitRateAllValues));

  memcpy(ret, bitRateAllValues, sizeof(bitRateAllValues));
  *ret_size = NUM_ELEMENTS_IN_ARRAY(bitRateAllValues);
  return ret;
}

audio_encoder_table_t faac_audio_encoder_table = {
  "AAC - FAAC", 
  AUDIO_ENCODER_FAAC,
  AUDIO_ENCODING_AAC,
  samplingRateAllValues,
  NUM_ELEMENTS_IN_ARRAY(samplingRateAllValues),
  faac_bitrates_for_samplerate,
  2
};

MediaType faac_mp4_fileinfo (CLiveConfig *pConfig,
			     bool *mpeg4,
			     bool *isma_compliant,
			     uint8_t *audioProfile,
			     uint8_t **audioConfig,
			     uint32_t *audioConfigLen,
			     uint8_t *mp4AudioType)
{
  *mpeg4 = true;
  *isma_compliant = true;
  *audioProfile = 0x0f;
  if (mp4AudioType) *mp4AudioType = MP4_MPEG4_AUDIO_TYPE;

  MP4AV_AacGetConfiguration(audioConfig,
			    audioConfigLen,
			    MP4AV_AAC_LC_PROFILE,
			    pConfig->GetIntegerValue(CONFIG_AUDIO_SAMPLE_RATE),
			    pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS));
  return AACAUDIOFRAME;
}

media_desc_t *faac_create_audio_sdp (CLiveConfig *pConfig,
				     bool *mpeg4,
				     bool *isma_compliant,
				     uint8_t *audioProfile,
				     uint8_t **audioConfig,
				     uint32_t *audioConfigLen)
{
  media_desc_t *sdpMediaAudio;
  format_list_t *sdpMediaAudioFormat;
  rtpmap_desc_t *sdpAudioRtpMap;
  char audioFmtpBuf[512];

  faac_mp4_fileinfo(pConfig, mpeg4, isma_compliant, audioProfile, audioConfig,
		    audioConfigLen, NULL);

  sdpMediaAudio = MALLOC_STRUCTURE(media_desc_t);
  memset(sdpMediaAudio, 0, sizeof(*sdpMediaAudio));

  sdp_add_string_to_list(&sdpMediaAudio->unparsed_a_lines,
			 "a=mpeg4-esid:10");
  sdpMediaAudioFormat = MALLOC_STRUCTURE(format_list_t);
  memset(sdpMediaAudioFormat, 0, sizeof(*sdpMediaAudioFormat));

  sdpMediaAudioFormat->media = sdpMediaAudio;
  sdpMediaAudioFormat->fmt = strdup("97");

  sdpAudioRtpMap = MALLOC_STRUCTURE(rtpmap_desc_t);
  memset(sdpAudioRtpMap, 0, sizeof(*sdpAudioRtpMap));
  sdpAudioRtpMap->clock_rate = 
    pConfig->GetIntegerValue(CONFIG_AUDIO_SAMPLE_RATE);
  sdpAudioRtpMap->encode_name = strdup("mpeg4-generic");
	      
  char* sConfig = 
    MP4BinaryToBase16(*audioConfig, *audioConfigLen);
	      
  sprintf(audioFmtpBuf,
	  "streamtype=5; profile-level-id=15; mode=AAC-hbr; config=%s; "
	  "SizeLength=13; IndexLength=3; IndexDeltaLength=3; Profile=1;",
	  sConfig); 
  free(sConfig);
	      
  sdpMediaAudioFormat->fmt_param = strdup(audioFmtpBuf);
  sdpMediaAudioFormat->rtpmap = sdpAudioRtpMap;

  sdpMediaAudio->fmt = sdpMediaAudioFormat;
  
  return sdpMediaAudio;
}

#define AAC_MAX_FRAME_IN_RTP_PAK 8
static bool faac_add_rtp_header (struct iovec *iov,
				 int queue_cnt,
				 void *ud)
{
  uint16_t numHdrBits = 16 * queue_cnt;
  int ix;
  uint8_t *aacHeader = (uint8_t *)ud;
  aacHeader[0] = numHdrBits >> 8;
  aacHeader[1] = numHdrBits & 0xff;

  for (ix = 1; ix <= queue_cnt; ix++) {
    aacHeader[2 + ((ix - 1) * 2)] = 
      iov[ix].iov_len >> 5;
    aacHeader[3 + ((ix - 1) * 2)] = 
      (iov[ix].iov_len & 0x1f) << 3;
  }
  iov[0].iov_base = aacHeader;
  iov[0].iov_len = 2 + (queue_cnt * 2);
  return true;
}

static bool faac_set_rtp_jumbo_frame (struct iovec *iov,
				      uint32_t dataOffset,
				      uint32_t bufferLen,
				      uint32_t rtpPayloadMax,
				      bool &mbit, 
				      void *ud)
{
  uint8_t *payloadHeader = (uint8_t *)ud;
  if (dataOffset == 0) {
    // first packet
    mbit = false;
    payloadHeader[0] = 0;
    payloadHeader[1] = 16;
    payloadHeader[2] = bufferLen >> 5;
    payloadHeader[3] = (bufferLen & 0x1f) << 3;
    iov[0].iov_base = payloadHeader;
    iov[0].iov_len = 4;
    iov[1].iov_len = MIN(rtpPayloadMax - 4, bufferLen);
    return true;
  } 
  mbit = false;
  iov[1].iov_len = MIN(bufferLen - dataOffset, rtpPayloadMax);
  return false;
}

bool faac_get_audio_rtp_info (CLiveConfig *pConfig,
			      MediaType *audioFrameType,
			      uint32_t *audioTimeScale,
			      uint8_t *audioPayloadNumber,
			      uint8_t *audioPayloadBytesPerPacket,
			      uint8_t *audioPayloadBytesPerFrame,
			      uint8_t *audioQueueMaxCount,
			      audio_set_rtp_header_f *audio_set_header,
			      audio_set_rtp_jumbo_frame_f *audio_set_jumbo,
			      void **ud)
{
  *audioFrameType = AACAUDIOFRAME;
  *audioTimeScale = pConfig->GetIntegerValue(CONFIG_AUDIO_SAMPLE_RATE);
  *audioPayloadNumber = 97;
  *audioPayloadBytesPerPacket = 2;
  *audioPayloadBytesPerFrame = 2;
  *audioQueueMaxCount = AAC_MAX_FRAME_IN_RTP_PAK;
  *audio_set_header = faac_add_rtp_header;
  *audio_set_jumbo = faac_set_rtp_jumbo_frame;
  *ud = malloc(2 + (2 * AAC_MAX_FRAME_IN_RTP_PAK));
  return true;
}

CFaacAudioEncoder::CFaacAudioEncoder()
{
  m_faacHandle = NULL;
  m_samplesPerFrame = 1024;
  m_aacFrameBuffer = NULL;
  m_aacFrameBufferLength = 0;
  m_aacFrameMaxSize = 0;
}

bool CFaacAudioEncoder::Init(CLiveConfig* pConfig, bool realTime)
{
  m_pConfig = pConfig;

  m_faacHandle = faacEncOpen(
                             m_pConfig->GetIntegerValue(CONFIG_AUDIO_SAMPLE_RATE),
                             m_pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS),
                             (unsigned long*)&m_samplesPerFrame,
                             (unsigned long*)&m_aacFrameMaxSize);

  if (m_faacHandle == NULL) {
    return false;
  }


  m_samplesPerFrame /= m_pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS);

  m_faacConfig = faacEncGetCurrentConfiguration(m_faacHandle);

  /*
    debug_message("version = %d", m_faacConfig->version);
    debug_message("name = %s", m_faacConfig->name);
    debug_message("allowMidside = %d", m_faacConfig->allowMidside);
    debug_message("useLfe = %d", m_faacConfig->useLfe);
    debug_message("useTns = %d", m_faacConfig->useTns);
    debug_message("bitRate = %lu", m_faacConfig->bitRate);
    debug_message("bandWidth = %d", m_faacConfig->bandWidth);
    debug_message("quantqual = %lu", m_faacConfig->quantqual);
    debug_message("outputFormat = %d", m_faacConfig->outputFormat);
    debug_message("psymodelidx = %d", m_faacConfig->psymodelidx);
    debug_message("inputFormat = %d", m_faacConfig->inputFormat);
  */

  m_faacConfig->mpegVersion = MPEG4;
  m_faacConfig->aacObjectType = LOW;
  m_faacConfig->allowMidside = false;
  m_faacConfig->useLfe = false;
  m_faacConfig->useTns = false;
  m_faacConfig->inputFormat = FAAC_INPUT_16BIT;
  m_faacConfig->outputFormat = 0;    // raw

  m_faacConfig->bitRate = 
    m_pConfig->GetIntegerValue(CONFIG_AUDIO_BIT_RATE)
    / m_pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS);

  faacEncSetConfiguration(m_faacHandle, m_faacConfig);

  return true;
}

u_int32_t CFaacAudioEncoder::GetSamplesPerFrame()
{
  return m_samplesPerFrame;
}

bool CFaacAudioEncoder::EncodeSamples(
                                      int16_t* pSamples, 
                                      u_int32_t numSamplesPerChannel,
                                      u_int8_t numChannels)
{
  if (numChannels != 1 && numChannels != 2) {
    return false;	// invalid numChannels
  }

  // check for signal to end encoding
  if (pSamples == NULL) {
    // unlike lame, faac doesn't need to finish up anything
    return false;
  }

  int16_t* pInputBuffer = pSamples;
  bool inputBufferMalloced = false;

  // free old AAC buffer, just in case, should already be NULL
 CHECK_AND_FREE(m_aacFrameBuffer);

  // allocate the AAC buffer
  m_aacFrameBuffer = (u_int8_t*)Malloc(m_aacFrameMaxSize);
  // check for channel mismatch between src and dst
  if (numChannels != m_pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS)) {
    if (numChannels == 1) {
      // convert mono to stereo
      pInputBuffer = NULL;
      inputBufferMalloced = true;

      InterleaveStereoSamples(
                              pSamples,
                              pSamples,
                              numSamplesPerChannel,
                              &pInputBuffer);

    } else { // numChannels == 2
      // convert stereo to mono
      pInputBuffer = NULL;
      inputBufferMalloced = true;

      DeinterleaveStereoSamples(
				pSamples,
				numSamplesPerChannel,
				&pInputBuffer,
				NULL);
    }
  }

  int rc = faacEncEncode(
                         m_faacHandle,
                         (int32_t *)pInputBuffer,
                         numSamplesPerChannel
                         * m_pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS),
                         m_aacFrameBuffer,
                         m_aacFrameMaxSize);

  if (inputBufferMalloced) {
    free(pInputBuffer);
    pInputBuffer = NULL;
  }

  if (rc < 0) {
    return false;
  }

  m_aacFrameBufferLength = rc;

  return true;
}

bool CFaacAudioEncoder::GetEncodedFrame(
                                        u_int8_t** ppBuffer, 
                                        u_int32_t* pBufferLength,
                                        u_int32_t* pNumSamplesPerChannel)
{
  if (m_aacFrameBufferLength == 0 && m_aacFrameBuffer) {
    free(m_aacFrameBuffer);
    m_aacFrameBuffer = NULL;
  }
  *ppBuffer = m_aacFrameBuffer;
  *pBufferLength = m_aacFrameBufferLength;
  *pNumSamplesPerChannel = m_samplesPerFrame;

  m_aacFrameBuffer = NULL;
  m_aacFrameBufferLength = 0;

  return true;
}

void CFaacAudioEncoder::Stop()
{
  faacEncClose(m_faacHandle);
  m_faacHandle = NULL;
  CHECK_AND_FREE(m_aacFrameBuffer);
}

