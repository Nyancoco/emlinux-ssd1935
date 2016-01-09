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
 *		Bill May 		wmay@cisco.com
 *		Peter Maersk-Moller	peter@maersk-moller.net
 */

#include "mp4live.h"
#include "rtp_transmitter.h"
#include "encoder-h261.h"

//#define RTP_DEBUG 1
CRtpTransmitter::CRtpTransmitter (CLiveConfig *pConfig) : CMediaSink()
{
  SetConfig(pConfig);

  m_destListMutex = SDL_CreateMutex();
  m_audioSrcPort = 0;
  m_videoSrcPort = 0;
  m_audioRtpDestination = NULL;
  m_audioPayloadNumber = 97;
  m_audioQueue = NULL;
  m_haveAudioStartTimestamp = false;
  m_audio_rtp_userdata = NULL;
  m_audio_set_rtp_payload = NULL;	// plugin function to determind how to build RTP payload
  m_audio_queue_frame = NULL;		// plugin function to determind how to queue frames for RTP
  m_frameno = NULL;			// plugin will allocate counter space
  m_audioiovMaxCount = 0;		// Max number of iov segments for send_iov
  if (m_pConfig->GetBoolValue(CONFIG_AUDIO_ENABLE)) {
	  
    if (m_audioSrcPort == 0) {
      m_audioSrcPort = GetRandomPortBlock();
    }
    if (m_pConfig->GetIntegerValue(CONFIG_RTP_AUDIO_DEST_PORT) == 0) {
      m_pConfig->SetIntegerValue(CONFIG_RTP_AUDIO_DEST_PORT,
				 m_audioSrcPort + 2);
    }
    if (get_audio_rtp_info(m_pConfig,
			   &m_audioFrameType,
			   &m_audioTimeScale,
			   &m_audioPayloadNumber, 
			   &m_audioPayloadBytesPerPacket,
			   &m_audioPayloadBytesPerFrame,
			   &m_audioQueueMaxCount,
			   &m_audioiovMaxCount,
			   &m_audio_queue_frame,
			   &m_audio_set_rtp_payload,
			   &m_audio_set_rtp_header,
			   &m_audio_set_rtp_jumbo_frame,
			   &m_audio_rtp_userdata) == false) {
      error_message("rtp transmitter: unknown audio encoding %s",
		    m_pConfig->GetStringValue(CONFIG_AUDIO_ENCODING));
    }

    if (m_audioiovMaxCount == 0)			// This is purely for backwards compability
      m_audioiovMaxCount = m_audioQueueMaxCount;	// Can go away when lame and faac plugin sets this

    if (m_pConfig->GetBoolValue(CONFIG_RTP_DISABLE_TS_OFFSET)) {
      m_audioRtpTimestampOffset = 0;
    } else {
      m_audioRtpTimestampOffset = random();
    }

    m_audioQueueCount = 0;
    m_audioQueueSize = 0;
    
    m_audioQueue = (CMediaFrame**)Malloc(m_audioQueueMaxCount * sizeof(CMediaFrame*));
  }

  // Initialize Video portion of transmitter
  m_videoRtpDestination = NULL;

  m_videoSendFunc = GetVideoRtpTransmitRoutine(pConfig, 
					       &m_videoFrameType, 
					       &m_videoPayloadNumber);
  m_videoTimeScale = 90000;
  m_haveVideoStartTimestamp = false;
  if (m_pConfig->GetBoolValue(CONFIG_VIDEO_ENABLE)) {

    if (m_pConfig->GetBoolValue(CONFIG_RTP_DISABLE_TS_OFFSET)) {
      m_videoRtpTimestampOffset = 0;
    } else {
      m_videoRtpTimestampOffset = random();
    }

    if (m_videoSrcPort == 0) {
      m_videoSrcPort = GetRandomPortBlock();
    }
    if (m_pConfig->GetIntegerValue(CONFIG_RTP_VIDEO_DEST_PORT) == 0) {
      m_pConfig->SetIntegerValue(CONFIG_RTP_VIDEO_DEST_PORT,
				 m_videoSrcPort + 2);
    }
  }
  
}

CRtpTransmitter::~CRtpTransmitter (void)
{
  CRtpDestination *p;
  p = m_audioRtpDestination;
  while (p != NULL) {
    p = p->get_next();
    delete m_audioRtpDestination;
    m_audioRtpDestination = p;
  }
  p = m_videoRtpDestination;
  while (p != NULL) {
    p = p->get_next();
    delete m_videoRtpDestination;
    m_videoRtpDestination = p;
  }
  SDL_DestroyMutex(m_destListMutex);
  CHECK_AND_FREE(m_audio_rtp_userdata);
  if (m_frameno != NULL) {
	free (m_frameno);
	m_frameno = NULL;		// Not strictly necessary
  }
}

void CRtpTransmitter::CreateAudioRtpDestination (uint32_t ref,
						 const char *destAddress,
						 in_port_t destPort,
						 in_port_t srcPort)
{
  CRtpDestination *adest, *p;
  debug_message("Creating rtp destination %s %d %d %d", 
		destAddress, destPort, ref, srcPort);
  if (srcPort == 0) srcPort = m_audioSrcPort;
  adest = new CRtpDestination(ref, 
			      destAddress,
			      destPort,
			      srcPort,
			      m_audioPayloadNumber,
			      m_pConfig->GetIntegerValue(CONFIG_RTP_MCAST_TTL),
			      DEFAULT_RTCP_BW);
  SDL_LockMutex(m_destListMutex);
  if (m_audioRtpDestination == NULL) {
    m_audioRtpDestination = adest;
  } else {
    p = m_audioRtpDestination;
    while (p->get_next() != NULL) {
      p = p->get_next();
    }
    p->set_next(adest);
  }
  SDL_UnlockMutex(m_destListMutex);
}

void CRtpTransmitter::CreateVideoRtpDestination (uint32_t ref,
						 const char *destAddress,
						 in_port_t destPort,
						 in_port_t srcPort)
{
  CRtpDestination *vdest, *p;
  if (srcPort == 0) srcPort = m_audioSrcPort;
  vdest = new CRtpDestination(ref, 
			      destAddress,
			      destPort,
			      srcPort,
			      m_videoPayloadNumber,
			      m_pConfig->GetIntegerValue(CONFIG_RTP_MCAST_TTL),
			      DEFAULT_RTCP_BW);
  SDL_LockMutex(m_destListMutex);
  if (m_videoRtpDestination == NULL) {
    m_videoRtpDestination = vdest;
  } else {
    p = m_videoRtpDestination;
    while (p->get_next() != NULL) {
      p = p->get_next();
    }
    p->set_next(vdest);
  }
  SDL_UnlockMutex(m_destListMutex);
}

int CRtpTransmitter::ThreadMain(void) 
{
  CMsg* pMsg;
  bool stop = false;
  while (stop == false && SDL_SemWait(m_myMsgQueueSemaphore) == 0) {
    pMsg = m_myMsgQueue.get_message();
    if (pMsg != NULL) {
      switch (pMsg->get_value()) {
      case MSG_NODE_STOP_THREAD:
	DoStopTransmit();
	stop = true;
	break;
      case MSG_NODE_START:
	DoStartTransmit();
	break;
      case MSG_NODE_STOP:
	DoStopTransmit();
	break;
      case MSG_SINK_FRAME:
	uint32_t dontcare;
	DoSendFrame((CMediaFrame*)pMsg->get_message(dontcare));
	break;
      case MSG_RTP_DEST_START:
	DoStartRtpDestination(pMsg->get_param());
	break;
      case MSG_RTP_DEST_STOP:
	DoStopRtpDestination(pMsg->get_param());
	break;
      }
      
      delete pMsg;
    }
  }
  while ((pMsg = m_myMsgQueue.get_message()) != NULL) {
    if (pMsg->get_value() == MSG_SINK_FRAME) {
      uint32_t dontcare;
      CMediaFrame *mf = (CMediaFrame*)pMsg->get_message(dontcare);
      if (mf->RemoveReference()) {
	delete mf;
      }
    }
    delete pMsg;
  }
  
  return 0;
}

void CRtpTransmitter::DoStartTransmit()
{
  if (m_sink) {
    return;
  }

  if (m_audioRtpDestination != NULL) {
    m_audioRtpDestination->start();
  }

  if (m_videoRtpDestination != NULL) {
    m_videoRtpDestination->start();
  }

  if (m_audioRtpDestination || m_videoRtpDestination) {
    // automatic sdp file generation
    GenerateSdpFile(m_pConfig);

    m_sink = true;
  }
}

void CRtpTransmitter::DoStopTransmit()
{
	if (!m_sink) {
		return;
	}

	if (m_audioRtpDestination) {
	  // send any pending frames
	  SendQueuedAudioFrames();

	  delete m_audioRtpDestination;
	  m_audioRtpDestination = NULL;
	  free(m_audioQueue);
	  m_audioQueue = NULL;
	}

	if (m_videoRtpDestination) {
	  delete m_videoRtpDestination;
	  m_videoRtpDestination = NULL;
	}

	m_sink = false;
}

void CRtpTransmitter::DoSendFrame(CMediaFrame* pFrame)
{
	if (pFrame == NULL) {
		return;
	}
	if (!m_sink) {
	  if (pFrame->RemoveReference())
		delete pFrame;
	  return;
	}

	if (pFrame->GetType() == m_audioFrameType && m_audioRtpDestination) {
		SendAudioFrame(pFrame);

	} else if (pFrame->GetType() == m_videoFrameType 
		   && m_videoRtpDestination) {
	  // Note - the below changed from the DTS to the PTS - this
	  // is required for b-frames, or mpeg2
	  u_int32_t rtpTimestamp =
	    VideoTimestampToRtp(pFrame->GetPtsTimestamp());
	  u_int64_t ntpTimestamp = 
	    TimestampToNtp(pFrame->GetTimestamp());
	  
	  CRtpDestination *rdptr;
	  
	  rdptr = m_videoRtpDestination;
	  while (rdptr != NULL) {
	    rdptr->send_rtcp(rtpTimestamp, ntpTimestamp);
	    rdptr = rdptr->get_next();
	  }
	  (m_videoSendFunc)(pFrame, m_videoRtpDestination, rtpTimestamp, 
			    m_pConfig->GetIntegerValue(CONFIG_RTP_PAYLOAD_SIZE));
	} else {
	  if (pFrame->RemoveReference())
		delete pFrame;
	}
}

// void CRtpTransmitter::SendAudioFrame(CMediaFrame* pFrame)
void CRtpTransmitter::OldSendAudioFrame(CMediaFrame* pFrame)
{
  //  debug_message("RTP Timestamp %u\tFrame Duration "U64,
  //                AudioTimestampToRtp(pFrame->GetTimestamp())
  //                , pFrame->GetDuration());

	// first compute how much data we'll have 
	// after adding this audio frame
	
  	//u_int16_t newAudioQueueSize = m_audioQueueSize;

        //	if (m_audioQueueCount == 0) {
        //		newAudioQueueSize += 
        //			m_audioPayloadBytesPerPacket;
        //	} else {
  //uint32_t ourTs = AudioTimestampToRtp(pFrame->GetTimestamp());
  //int32_t diff = ourTs - m_nextAudioRtpTimestamp;

  //if (diff > 1) {
  //  debug_message("Timestamp not consecutive error - timestamp "U64" should be %u is %u", 
  //                pFrame->GetTimestamp(),
  //                m_nextAudioRtpTimestamp,
  //                ourTs);
	    //***   SendQueuedAudioFrames();
            //	    newAudioQueueSize = m_audioQueueSize +=
            //	      m_audioPayloadBytesPerPacket;
  //}

          //	}

  /*
	// save the next timestamp.
	if (m_audioTimeScale == pFrame->GetDurationScale()) {
	  m_nextAudioRtpTimestamp = 
	    AudioTimestampToRtp(pFrame->GetTimestamp()) + 
	    pFrame->GetDuration();
	} else {
	  m_nextAudioRtpTimestamp = 
	    AudioTimestampToRtp(pFrame->GetTimestamp()) + 
	    pFrame->ConvertDuration(m_audioTimeScale);
	}
  */
        uint16_t newAudioQueueSize = m_audioQueueSize
          + m_audioPayloadBytesPerPacket
          + m_audioPayloadBytesPerFrame
          + pFrame->GetDataLength();

	// if new queue size exceeds the RTP payload
	if (newAudioQueueSize 
	  > m_pConfig->GetIntegerValue(CONFIG_RTP_PAYLOAD_SIZE)) {

		// send anything that's pending
		SendQueuedAudioFrames();

		// adjust new queue size
		newAudioQueueSize =
			m_audioPayloadBytesPerPacket
			+ m_audioPayloadBytesPerFrame
			+ pFrame->GetDataLength();
	}

	// if new data will fit into RTP payload
	if (newAudioQueueSize 
	  <= m_pConfig->GetIntegerValue(CONFIG_RTP_PAYLOAD_SIZE)) {

		// add it to the queue
		m_audioQueue[m_audioQueueCount++] = pFrame;
		m_audioQueueSize = newAudioQueueSize;

		// if we fill the queue, (latency bound)
		if (m_audioQueueCount == m_audioQueueMaxCount) {
			// send the pending data
			SendQueuedAudioFrames();
		}
	} else {
		// highly unusual case 
		// we need to fragment audio frame
		// over multiple packets
		SendAudioJumboFrame(pFrame);
	}
}

/*
 * #define DROP_IT	1
 * #define SEND_FIRST	2
 * #define IS_JUMBO	4
 * #define SEND_NOW	8
 */
void CRtpTransmitter::SendAudioFrame(CMediaFrame* pFrame)
{
	if (m_audio_queue_frame != NULL) {

		// Get status for what we should do with the next frame
		int check_frame = m_audio_queue_frame(&m_frameno,
			pFrame->GetDataLength(),
			m_audioQueueCount, m_audioQueueSize,
			(u_int32_t) m_pConfig->GetIntegerValue(CONFIG_RTP_PAYLOAD_SIZE));

#ifdef RTP_DEBUG
		fprintf(stderr,"Check Frame :");
		if (check_frame & DROP_IT) fprintf(stderr," DROP");
		if (check_frame & SEND_FIRST) fprintf(stderr," FIRST");
		if (check_frame & IS_JUMBO) fprintf(stderr," JUMBO");
		if (check_frame == 0 || !((check_frame & DROP_IT) ||
		  (check_frame & IS_JUMBO))) fprintf(stderr," QUEUE");
		if (check_frame & SEND_NOW) fprintf(stderr," NOW");
		fprintf(stderr,"\n");
#endif	// RTP_DEBUG

		// Check and see if we should send it or drop it
		if (check_frame & DROP_IT)
			return;

		// Check and see if we should send the queue first
		if (m_audioQueueCount > 0 && (check_frame & SEND_FIRST)) {

			// send anything that's pending
			SendQueuedAudioFrames();
		}

		// See if we have a jumbo frame
		if (check_frame & IS_JUMBO) {
			SendAudioJumboFrame(pFrame);
			return;
		}

		// Add frame to queue
		m_audioQueue[m_audioQueueCount++] = pFrame;
		m_audioQueueSize += pFrame->GetDataLength();

		// and check to see if we want ot send it now
		if (check_frame & SEND_NOW) {

			// send anything that's pending
			SendQueuedAudioFrames();
		}

	} else {

		// B & D 's original CRtpTransmitter::SendAudioFrame
		OldSendAudioFrame(pFrame);
	}
}

void CRtpTransmitter::SendQueuedAudioFrames(void)
{
	struct iovec iov[m_audioiovMaxCount + 1];
	int ix;
	int i = 0;
	CRtpDestination *rdptr;

	if (m_audioQueueCount == 0) {
		return;
	}

	u_int32_t rtpTimestamp =
		AudioTimestampToRtp(m_audioQueue[0]->GetTimestamp());
	u_int64_t ntpTimestamp =
		TimestampToNtp(m_audioQueue[0]->GetTimestamp());

#if RTP_DEBUG
	debug_message("A ts "U64" rtp %u ntp %u.%u",
		m_audioQueue[0]->GetTimestamp(),
		rtpTimestamp,
		(u_int32_t)(ntpTimestamp >> 32),
		(u_int32_t)ntpTimestamp);
#endif
	rdptr = m_audioRtpDestination;
	while (rdptr != NULL) {
	  rdptr->send_rtcp(rtpTimestamp, ntpTimestamp);
	  rdptr = rdptr->get_next();
	}

	// moved this from below
	// See if we need to add the header.
	int iov_start = 1;
	int iov_add = 0;

	if (m_audio_set_rtp_payload != NULL) {
		iov_start = 0;
		iov_add = m_audio_set_rtp_payload(m_audioQueue,
					m_audioQueueCount,
					iov,
					m_audio_rtp_userdata);
	} else {
		// audio payload data
		for (i = 0; i < m_audioQueueCount; i++) {
	  	iov[i + 1].iov_base = m_audioQueue[i]->GetData();
	  	iov[i + 1].iov_len = m_audioQueue[i]->GetDataLength();
		}

		if (m_audio_set_rtp_header != NULL) {
	  		if ((m_audio_set_rtp_header)(iov, 
				       	m_audioQueueCount, 
				       	m_audio_rtp_userdata)) {
	    			iov_start = 0;
	    			iov_add = 1;
	  		} 
		}
	}
		
	// send packet
	rdptr = m_audioRtpDestination;
	while (rdptr != NULL) {
	  rdptr->send_iov(&iov[iov_start],
			  iov_add + m_audioQueueCount,
			  rtpTimestamp,
			  1);
	  rdptr = rdptr->get_next();
	}

	// delete all the pending media frames
	for (ix = 0; ix < m_audioQueueCount; ix++) {
	  if (m_audioQueue[ix]->RemoveReference())
	    delete m_audioQueue[ix];
	  m_audioQueue[ix] = NULL;
	}

	m_audioQueueCount = 0;
	m_audioQueueSize = 0;
}

void CRtpTransmitter::SendAudioJumboFrame(CMediaFrame* pFrame)
{
  u_int32_t rtpTimestamp = 
    AudioTimestampToRtp(pFrame->GetTimestamp());
  u_int64_t ntpTimestamp =
    TimestampToNtp(pFrame->GetTimestamp());
  CRtpDestination *rdptr;

#if RTP_DEBUG
  debug_message("A ts "U64" rtp %u ntp %u.%u",
		pFrame->GetTimestamp(),
		rtpTimestamp,
		(u_int32_t)(ntpTimestamp >> 32),
		(u_int32_t)ntpTimestamp);
#endif
  rdptr = m_audioRtpDestination;
  while (rdptr != NULL) {
    rdptr->send_rtcp(rtpTimestamp, ntpTimestamp);
    rdptr = rdptr->get_next();
  }

  u_int32_t dataOffset = 0;
  u_int32_t spaceAvailable = 
    m_pConfig->GetIntegerValue(CONFIG_RTP_PAYLOAD_SIZE);


  struct iovec iov[2];
  do {
    iov[1].iov_base = (u_int8_t*)pFrame->GetData() + dataOffset;
    int iov_start = 1, iov_hdr = 0;
    bool mbit;
    if (m_audio_set_rtp_jumbo_frame(iov,
				    dataOffset, 
				    pFrame->GetDataLength(),
				    spaceAvailable,
				    mbit,
				    m_audio_rtp_userdata)) {
      iov_start = 0;
      iov_hdr = 1;
    }
    // send packet
    rdptr = m_audioRtpDestination;
    while (rdptr != NULL) {
      rdptr->send_iov(&iov[iov_start],
		      1 + iov_hdr,
		      rtpTimestamp,
		      mbit);
      rdptr = rdptr->get_next();
    }
    error_message("data offset %d len %d max %d", dataOffset, 
		  (int)iov[1].iov_len,
		  pFrame->GetDataLength());
    dataOffset += iov[1].iov_len;
  } while (dataOffset < pFrame->GetDataLength());

  if (pFrame->RemoveReference())
    delete pFrame;
}




void CRtpTransmitter::DoStartRtpDestination (uint32_t handle)
{
  CRtpDestination *ptr;

  ptr = m_videoRtpDestination;
  while (ptr != NULL) {
    if (ptr->get_ref_num() == handle) {
      ptr->start();
      return;
    }
    ptr = ptr->get_next();
  }
  ptr = m_audioRtpDestination;
  while (ptr != NULL) {
    if (ptr->get_ref_num() == handle) {
      ptr->start();
      debug_message("Starting rtp handle %d", handle);
      return;
    }
    ptr = ptr->get_next();
  }
}

void CRtpTransmitter::DoStopRtpDestination (uint32_t handle)
{
  CRtpDestination *ptr, *q;

  q = NULL;
  ptr = m_videoRtpDestination;
  while (ptr != NULL) {
    if (ptr->get_ref_num() == handle) {
      if (q == NULL) {
	m_videoRtpDestination = ptr->get_next();
      } else {
	q->set_next(ptr->get_next());
      }
      delete ptr;
      return;
    }
    q = ptr;
    ptr = ptr->get_next();
  }

  q = NULL;
  ptr = m_audioRtpDestination;
  while (ptr != NULL) {
    if (ptr->get_ref_num() == handle) {
      if (q == NULL) {
	m_audioRtpDestination = ptr->get_next();
      } else {
	q->set_next(ptr->get_next());
      }
      debug_message("Stopping rtp handle %d", handle);
      delete ptr;
      return;
    }
    q = ptr;
    ptr = ptr->get_next();
  }
}

static void RtpCallback (struct rtp *session, rtp_event *e) 
{
  // Currently we ignore RTCP packets
  // Just do our required housekeeping
  if (e && (e->type == RX_RTP || e->type == RX_APP)) {
    free(e->data);
  }
}

CRtpDestination::CRtpDestination (uint32_t ref_number,
				  const char *destAddr, 
				  in_port_t destPort,
				  in_port_t srcPort,
				  uint8_t payloadNumber,
				  int mcast_ttl,
				  float rtcp_bandwidth)
{

  m_refNum = ref_number;
  m_destAddr = strdup(destAddr);
  m_destPort = destPort;
  m_srcPort = srcPort;
  m_payloadNumber = payloadNumber;
  m_mcast_ttl = mcast_ttl;
  m_rtcp_bandwidth = rtcp_bandwidth;
  m_rtpSession = NULL;
  m_next = NULL;
}

void CRtpDestination::start (void) 
{

  while (m_rtpSession == NULL) {
    debug_message("Starting rtp dest %s %d %d", 
		  m_destAddr, m_destPort, m_srcPort);
    if (m_srcPort == 0) m_srcPort = m_destPort;
    m_rtpSession = rtp_init_xmitter(m_destAddr,
				    m_srcPort,
				    m_destPort,
				    m_mcast_ttl, 
				    m_rtcp_bandwidth, 
				    RtpCallback, (uint8_t *)this);
    if (m_rtpSession == NULL) {
      error_message("Couldn't start rtp session");
      SDL_Delay(10);
    }
  }
}
			  
			  
CRtpDestination::~CRtpDestination (void)
{
  CHECK_AND_FREE(m_destAddr);

  if (m_rtpSession != NULL) {
    rtp_done(m_rtpSession);
    m_rtpSession = NULL;
  }
}

void CRtpDestination::send_rtcp (u_int32_t rtpTimestamp,
				 u_int64_t ntpTimestamp)
{
  if (m_rtpSession != NULL) {
    rtp_send_ctrl_2(m_rtpSession, rtpTimestamp, ntpTimestamp, NULL);
    rtp_update(m_rtpSession);
  }
}

int CRtpDestination::send_iov (struct iovec *piov,
			       int iovCount,
			       u_int32_t rtpTimestamp,
			       int mbit)
{
  if (m_rtpSession != NULL) {
    return rtp_send_data_iov(m_rtpSession,
			     rtpTimestamp,
			     m_payloadNumber,
			     mbit, 
			     0, 
			     NULL, 
			     piov, 
			     iovCount,
			     NULL, 
			     0, 
			     0,
			     0);
  }
  return -1;
}
