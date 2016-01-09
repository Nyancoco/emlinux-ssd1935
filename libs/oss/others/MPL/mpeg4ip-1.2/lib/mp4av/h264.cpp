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
 * Copyright (C) Cisco Systems Inc. 2004.  All Rights Reserved.
 * 
 * Contributor(s): 
 *		Bill May wmay@cisco.com
 */

#include "mpeg4ip.h"
#include "mp4av_h264.h"
#include "bitstream.h"
//#define BOUND_VERBOSE

static uint8_t exp_golomb_bits[256] = {
8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 
3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 
2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 
2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 
};

uint32_t h264_ue (CBitstream *bs)
{
  uint32_t bits, read;
  uint8_t coded;

  bits = 0;
  while ((read = bs->PeekBits(8)) == 0) {
    bs->GetBits(8);
    bits += 8;
  }
  coded = exp_golomb_bits[read];
  bs->GetBits(coded);
  bits += coded;

  //  printf("ue - bits %d\n", bits);
  return bs->GetBits(bits + 1) - 1;
}

int32_t h264_se (CBitstream *bs) 
{
  uint32_t ret;
  ret = h264_ue(bs);
  if ((ret & 0x1) == 0) {
    ret >>= 1;
    int32_t temp = 0 - ret;
    return temp;
  } 
  return (ret + 1) >> 1;
}

extern "C" bool h264_is_start_code (const uint8_t *pBuf) 
{
  if (pBuf[0] == 0 && 
      pBuf[1] == 0 && 
      ((pBuf[2] == 1) ||
       ((pBuf[2] == 0) && pBuf[3] == 1))) {
    return true;
  }
  return false;
}

extern "C" uint32_t h264_find_next_start_code (const uint8_t *pBuf, 
					       uint32_t bufLen)
{
  uint32_t val, temp;
  uint32_t offset;

  offset = 0;
  if (pBuf[0] == 0 && 
      pBuf[1] == 0 && 
      ((pBuf[2] == 1) ||
       ((pBuf[2] == 0) && pBuf[3] == 1))) {
    pBuf += 3;
    offset = 3;
  }
  val = 0xffffffff;
  while (offset < bufLen - 3) {
    val <<= 8;
    temp = val & 0xff000000;
    val &= 0x00ffffff;
    val |= *pBuf++;
    offset++;
    if (val == H264_START_CODE) {
      if (temp == 0) return offset - 4;
      return offset - 3;
    }
  }
  return 0;
}

extern "C" uint8_t h264_nal_unit_type (const uint8_t *buffer)
{
  uint32_t offset;
  if (buffer[2] == 1) offset = 3;
  else offset = 4;
  return buffer[offset] & 0x1f;
}

extern "C" int h264_nal_unit_type_is_slice (uint8_t type)
{
  if (type >= H264_NAL_TYPE_NON_IDR_SLICE && 
      type <= H264_NAL_TYPE_IDR_SLICE) {
    return true;
  }
  return false;
}

/*
 * determine if the slice we decoded is a sync point
 */
extern "C" bool h264_slice_is_idr (h264_decode_t *dec) 
{
  if (dec->nal_unit_type != H264_NAL_TYPE_IDR_SLICE)
    return false;
  if (H264_TYPE_IS_I(dec->slice_type)) return true;
  if (H264_TYPE_IS_SI(dec->slice_type)) return true;
  return false;
}

extern "C" uint8_t h264_nal_ref_idc (const uint8_t *buffer)
{
  uint32_t offset;
  if (buffer[2] == 1) offset = 3;
  else offset = 4;
  return (buffer[offset] >> 5) & 0x3;
}

int h264_read_seq_info (const uint8_t *buffer, 
			uint32_t buflen, 
			h264_decode_t *dec)
{
  CBitstream bs;
  uint32_t header;

  if (buffer[2] == 1) header = 4;
  else header = 5;

  bs.init(buffer + header, (buflen - header) * 8);
  //bs.set_verbose(true);
  try {
    bs.GetBits(8 + 1 + 1 + 1 + 5 + 8);
    h264_ue(&bs); // seq_parameter_set_id
    dec->log2_max_frame_num_minus4 = h264_ue(&bs);
    dec->pic_order_cnt_type = h264_ue(&bs);
    if (dec->pic_order_cnt_type == 0) {
      dec->log2_max_pic_order_cnt_lsb_minus4 = h264_ue(&bs);
    } else if (dec->pic_order_cnt_type == 1) {
      dec->delta_pic_order_always_zero_flag = bs.GetBits(1);
      h264_se(&bs); // offset_for_non_ref_pic
      h264_se(&bs); // offset_for_top_to_bottom_field
      uint32_t temp = h264_ue(&bs);
      for (uint32_t ix = 0; ix < temp; ix++) {
	h264_se(&bs); // offset for ref fram -
      }
    }
    h264_ue(&bs); // num_ref_frames
    bs.GetBits(1); // gaps_in_frame_num_value_allowed_flag
    uint32_t PicWidthInMbs = h264_ue(&bs) + 1;
    dec->pic_width = PicWidthInMbs * 16;
    uint32_t PicHeightInMapUnits = h264_ue(&bs) + 1;

    dec->frame_mbs_only_flag = bs.GetBits(1);
    dec->pic_height = 
      (2 - dec->frame_mbs_only_flag) * PicHeightInMapUnits * 16;
#if 0
    if (!dec->frame_mbs_only_flag) {
      printf("    mb_adaptive_frame_field_flag: %u\n", bs->GetBits(1));
    }
    printf("   direct_8x8_inference_flag: %u\n", bs->GetBits(1));
    temp = bs->GetBits(1);
    printf("   frame_cropping_flag: %u\n", bs->GetBits(1));
    if (temp) {
      printf("     frame_crop_left_offset: %u\n", h264_ue(bs));
      printf("     frame_crop_right_offset: %u\n", h264_ue(bs));
      printf("     frame_crop_top_offset: %u\n", h264_ue(bs));
      printf("     frame_crop_bottom_offset: %u\n", h264_ue(bs));
    }
    temp = bs->GetBits(1);
    printf("   vui_parameters_present_flag: %u\n", temp);
    if (temp) {
      h264_vui_parameters(bs);
    }
#endif
  } catch (...) {
    return -1;
  }
  return 0;
}
int h264_read_slice_info (const uint8_t *buffer, 
			  uint32_t buflen, 
			  h264_decode_t *dec)
{
  uint32_t header;
  if (buffer[2] == 1) header = 4;
  else header = 5;
  CBitstream bs;
  bs.init(buffer + header, (buflen - header) * 8);
  try {
    dec->field_pic_flag = 0;
    dec->bottom_field_flag = 0;
    dec->delta_pic_order_cnt[0] = 0;
    dec->delta_pic_order_cnt[1] = 0;
    h264_ue(&bs); // first_mb_in_slice
    dec->slice_type = h264_ue(&bs); // slice type
    h264_ue(&bs); // pic_parameter_set
    dec->frame_num = bs.GetBits(dec->log2_max_frame_num_minus4 + 4);
    if (!dec->frame_mbs_only_flag) {
      dec->field_pic_flag = bs.GetBits(1);
      if (dec->field_pic_flag) {
	dec->bottom_field_flag = bs.GetBits(1);
      }
    }
    if (dec->nal_unit_type == H264_NAL_TYPE_IDR_SLICE) {
      dec->idr_pic_id = h264_ue(&bs);
    }
    switch (dec->pic_order_cnt_type) {
    case 0:
      dec->pic_order_cnt_lsb = bs.GetBits(dec->log2_max_pic_order_cnt_lsb_minus4 + 4);
      if (dec->pic_order_present_flag && !dec->field_pic_flag) {
	dec->delta_pic_order_cnt_bottom = h264_se(&bs);
      }
      break;
    case 1:
      if (!dec->delta_pic_order_always_zero_flag) {
	dec->delta_pic_order_cnt[0] = h264_se(&bs);
      }
      if (dec->pic_order_present_flag && !dec->field_pic_flag) {
	dec->delta_pic_order_cnt[1] = h264_se(&bs);
      }
      break;
    }

  } catch (...) {
    return -1;
  }
  return 0;
}
extern "C" int h264_detect_boundary (const uint8_t *buffer, 
				     uint32_t buflen, 
				     h264_decode_t *decode)
{
  uint8_t temp;
  h264_decode_t new_decode;
  int ret;

  memcpy(&new_decode, decode, sizeof(new_decode));

  temp = new_decode.nal_unit_type = h264_nal_unit_type(buffer);
  new_decode.nal_ref_idc = h264_nal_ref_idc(buffer);
  ret = 0;
  switch (temp) {
  case H264_NAL_TYPE_ACCESS_UNIT:
  case H264_NAL_TYPE_END_OF_SEQ:
  case H264_NAL_TYPE_END_OF_STREAM:
#ifdef BOUND_VERBOSE
    printf("nal type %d\n", temp);
#endif
    ret = 1;
    break;
  case H264_NAL_TYPE_NON_IDR_SLICE:
  case H264_NAL_TYPE_DP_A_SLICE:
  case H264_NAL_TYPE_DP_B_SLICE:
  case H264_NAL_TYPE_DP_C_SLICE:
  case H264_NAL_TYPE_IDR_SLICE:
    // slice buffer - read the info into the new_decode, and compare.
    if (h264_read_slice_info(buffer, buflen, &new_decode) < 0) {
      // need more memory
      return -1;
    }
    if (decode->nal_unit_type > H264_NAL_TYPE_IDR_SLICE || 
	decode->nal_unit_type < H264_NAL_TYPE_NON_IDR_SLICE) {
      break;
    }
    if (decode->frame_num != new_decode.frame_num) {
#ifdef BOUND_VERBOSE
      printf("frame num values different\n");
#endif
      ret = 1;
      break;
    }
    if (decode->field_pic_flag != new_decode.field_pic_flag) {
      ret = 1;
#ifdef BOUND_VERBOSE
      printf("field pic values different\n");
#endif
      break;
    }
    if (decode->nal_ref_idc != new_decode.nal_ref_idc &&
	(decode->nal_ref_idc == 0 ||
	 new_decode.nal_ref_idc == 0)) {
#ifdef BOUND_VERBOSE
      printf("nal ref idc values differ\n");
#endif
      ret = 1;
      break;
    }
    if (decode->frame_num == new_decode.frame_num &&
	decode->pic_order_cnt_type == new_decode.pic_order_cnt_type) {
      if (decode->pic_order_cnt_type == 0) {
	if (decode->pic_order_cnt_lsb != new_decode.pic_order_cnt_lsb) {
#ifdef BOUND_VERBOSE
	  printf("pic order 1\n");
#endif
	  ret = 1;
	  break;
	}
	if (decode->delta_pic_order_cnt_bottom != new_decode.delta_pic_order_cnt_bottom) {
	  ret = 1;
#ifdef BOUND_VERBOSE
	  printf("delta pic order cnt bottom 1\n");
#endif
	  break;
	}
      } else if (decode->pic_order_cnt_type == 1) {
	if (decode->delta_pic_order_cnt[0] != new_decode.delta_pic_order_cnt[0]) {
	  ret =1;
#ifdef BOUND_VERBOSE
	  printf("delta pic order cnt [0]\n");
#endif
	  break;
	}
	if (decode->delta_pic_order_cnt[1] != new_decode.delta_pic_order_cnt[1]) {
	  ret = 1;
#ifdef BOUND_VERBOSE
	  printf("delta pic order cnt [1]\n");
#endif
	  break;
	  
	}
      }
    }
    if (decode->nal_unit_type == H264_NAL_TYPE_IDR_SLICE &&
	new_decode.nal_unit_type == H264_NAL_TYPE_IDR_SLICE) {
      if (decode->idr_pic_id != new_decode.idr_pic_id) {
#ifdef BOUND_VERBOSE
	printf("idr_pic id\n");
#endif
	
	ret = 1;
	break;
      }
    }
    break;
  case H264_NAL_TYPE_SEQ_PARAM:
    if (h264_read_seq_info(buffer, buflen, &new_decode) < 0) {
      return -1;
    }
    // fall through
  default:
    if (decode->nal_unit_type <= H264_NAL_TYPE_IDR_SLICE) ret = 1;
    else ret = 0;
  } 
  // other types (6, 7, 8, 
  memcpy(decode, &new_decode, sizeof(*decode));
  return ret;
}

uint32_t h264_read_sei_value (const uint8_t *buffer, uint32_t *size) 
{
  uint32_t ret = 0;
  *size = 1;
  while (buffer[*size] == 0xff) {
    ret += 255;
    *size = *size + 1;
  }
  ret += *buffer;
  return ret;
}
