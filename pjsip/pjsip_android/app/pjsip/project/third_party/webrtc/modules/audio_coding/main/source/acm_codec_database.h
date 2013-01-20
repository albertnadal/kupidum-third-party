/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 * This file generates databases with information about all supported audio
 * codecs.
 */

#ifndef WEBRTC_MODULES_AUDIO_CODING_MAIN_SOURCE_ACM_CODEC_DATABASE_H_
#define WEBRTC_MODULES_AUDIO_CODING_MAIN_SOURCE_ACM_CODEC_DATABASE_H_

#include "acm_generic_codec.h"
#include "common_types.h"
#include "webrtc_neteq.h"

namespace webrtc {

// TODO(tlegrand): replace class ACMCodecDB with a namespace.
class ACMCodecDB {
 public:
  // Enum with array indexes for the supported codecs. NOTE! The order MUST
  // be the same as when creating the database in acm_codec_database.cc.
  enum {
    kNone = -1
#if (defined(WEBRTC_CODEC_ISAC) || defined(WEBRTC_CODEC_ISACFX))
    , kISAC
# if (defined(WEBRTC_CODEC_ISAC))
    , kISACSWB
# endif
#endif
#ifdef WEBRTC_CODEC_PCM16
    , kPCM16B
    , kPCM16Bwb
    , kPCM16Bswb32kHz
#endif
    , kPCMU
    , kPCMA
#ifdef WEBRTC_CODEC_ILBC
    , kILBC
#endif
#ifdef WEBRTC_CODEC_AMR
    , kGSMAMR
#endif
#ifdef WEBRTC_CODEC_AMRWB
    , kGSMAMRWB
#endif
#ifdef WEBRTC_CODEC_G722
    , kG722
#endif
#ifdef WEBRTC_CODEC_G722_1
    , kG722_1_32
    , kG722_1_24
    , kG722_1_16
#endif
#ifdef WEBRTC_CODEC_G722_1C
    , kG722_1C_48
    , kG722_1C_32
    , kG722_1C_24
#endif
#ifdef WEBRTC_CODEC_G729
    , kG729
#endif
#ifdef WEBRTC_CODEC_G729_1
    , kG729_1
#endif
#ifdef WEBRTC_CODEC_GSMFR
    , kGSMFR
#endif
#ifdef WEBRTC_CODEC_SPEEX
    , kSPEEX8
    , kSPEEX16
#endif
    , kCNNB
    , kCNWB
    , kCNSWB
#ifdef WEBRTC_CODEC_AVT
    , kAVT
#endif
#ifdef WEBRTC_CODEC_RED
    , kRED
#endif
    , kNumCodecs
  };

  // Set unsupported codecs to -1
#ifndef WEBRTC_CODEC_ISAC
  enum {kISACSWB = -1};
# ifndef WEBRTC_CODEC_ISACFX
  enum {kISAC = -1};
# endif
#endif
#ifndef WEBRTC_CODEC_PCM16
  enum {kPCM16B = -1};
  enum {kPCM16Bwb = -1};
  enum {kPCM16Bswb32kHz = -1};
#endif
  // 48 kHz not supported, always set to -1.
  enum {kPCM16Bswb48kHz = -1};
#ifndef WEBRTC_CODEC_ILBC
  enum {kILBC = -1};
#endif
#ifndef WEBRTC_CODEC_AMR
  enum {kGSMAMR = -1};
#endif
#ifndef WEBRTC_CODEC_AMRWB
  enum {kGSMAMRWB = -1};
#endif
#ifndef WEBRTC_CODEC_G722
  enum {kG722 = -1};
#endif
#ifndef WEBRTC_CODEC_G722_1
  enum {kG722_1_32 = -1};
  enum {kG722_1_24 = -1};
  enum {kG722_1_16 = -1};
#endif
#ifndef WEBRTC_CODEC_G722_1C
  enum {kG722_1C_48 = -1};
  enum {kG722_1C_32 = -1};
  enum {kG722_1C_24 = -1};
#endif
#ifndef WEBRTC_CODEC_G729
  enum {kG729 = -1};
#endif
#ifndef WEBRTC_CODEC_G729_1
  enum {kG729_1 = -1};
#endif
#ifndef WEBRTC_CODEC_GSMFR
  enum {kGSMFR = -1};
#endif
#ifndef WEBRTC_CODEC_SPEEX
  enum {kSPEEX8 = -1};
  enum {kSPEEX16 = -1};
#endif
#ifndef WEBRTC_CODEC_AVT
  enum {kAVT = -1};
#endif
#ifndef WEBRTC_CODEC_RED
  enum {kRED = -1};
#endif

  // kMaxNumCodecs - Maximum number of codecs that can be activated in one
  //                 build.
  // kMaxNumPacketSize - Maximum number of allowed packet sizes for one codec.
  // These might need to be increased if adding a new codec to the database
  static const int kMaxNumCodecs =  50;
  static const int kMaxNumPacketSize = 6;

  // Codec specific settings
  //
  // num_packet_sizes     - number of allowed packet sizes.
  // packet_sizes_samples - list of the allowed packet sizes.
  // basic_block_samples  - assigned a value different from 0 if the codec
  //                        requires to be fed with a specific number of samples
  //                        that can be different from packet size.
  // channel_support      - number of channels supported to encode;
  //                        1 = mono, 2 = stereo, etc.
  struct CodecSettings {
    int num_packet_sizes;
    int packet_sizes_samples[kMaxNumPacketSize];
    int basic_block_samples;
    int channel_support;
  };

  // Gets codec information from database at the position in database given by
  // [codec_id].
  // Input:
  //   [codec_id] - number that specifies at what position in the database to
  //                get the information.
  // Output:
  //   [codec_inst] - filled with information about the codec.
  // Return:
  //   0 if successful, otherwise -1.
  static int Codec(int codec_id, CodecInst* codec_inst);

  // Returns codec id and mirror id from database, given the information
  // received in the input [codec_inst]. Mirror id is a number that tells
  // where to find the codec's memory (instance). The number is either the
  // same as codec id (most common), or a number pointing at a different
  // entry in the database, if the codec has several entries with different
  // payload types. This is used for codecs that must share one struct even if
  // the payload type differs.
  // One example is the codec iSAC which has the same struct for both 16 and
  // 32 khz, but they have different entries in the database. Let's say the
  // function is called with iSAC 32kHz. The function will return 1 as that is
  // the entry in the data base, and [mirror_id] = 0, as that is the entry for
  // iSAC 16 kHz, which holds the shared memory.
  // Input:
  //   [codec_inst] - Information about the codec for which we require the
  //                  database id.
  // Output:
  //   [mirror_id] - mirror id, which most often is the same as the return
  //                 value, see above.
  //   [err_message] - if present, in the event of a mismatch found between the
  //                   input and the database, a descriptive error message is
  //                   written here.
  //   [err_message] - if present, the length of error message is returned here.
  // Return:
  //   codec id if successful, otherwise < 0.
  static int CodecNumber(const CodecInst* codec_inst, int* mirror_id,
                         char* err_message, int max_message_len_byte);
  static int CodecNumber(const CodecInst* codec_inst, int* mirror_id);
  static int ReceiverCodecNumber(const CodecInst* codec_inst, int* mirror_id);

  // Returns the codec sampling frequency for codec with id = "codec_id" in
  // database.
  // TODO(tlegrand): Check if function is needed, or if we can change
  // to access database directly.
  // Input:
  //   [codec_id] - number that specifies at what position in the database to
  //                get the information.
  // Return:
  //   codec sampling frequency if successful, otherwise -1.
  static int CodecFreq(int codec_id);

  // Return the codec's basic coding block size in samples.
  // TODO(tlegrand): Check if function is needed, or if we can change
  // to access database directly.
  // Input:
  //   [codec_id] - number that specifies at what position in the database to
  //                get the information.
  // Return:
  //   codec basic block size if successful, otherwise -1.
  static int BasicCodingBlock(int codec_id);

  // Returns the NetEQ decoder database.
  static WebRtcNetEQDecoder* NetEQDecoders();

  // All version numbers for the codecs in the database are listed in text.
  // Input/Output:
  //   [version] - pointer to a char vector with minimum size 1000 bytes.
  //               Audio coding module's and all component's versions is
  //               written here.
  //   [remaining_buffer_bytes] - remaining space in buffer.
  //   [position] - current position to write at in buffer.
  // Return:
  //   -1 if version information doesn't fit, 0 on success.
  static int CodecsVersion(char* version, size_t* remaining_buffer_bytes,
                           size_t* position);

  // Returns mirror id, which is a number that tells where to find the codec's
  // memory (instance). It is either the same as codec id (most common), or a
  // number pointing at a different entry in the database, if the codec have
  // several entries with different payload types. This is used for codecs that
  // must share struct even if the payload type differs.
  // TODO(tlegrand): Check if function is needed, or if we can change
  // to access database directly.
  // Input:
  //   [codec_id] - number that specifies codec's position in the database.
  // Return:
  //   Mirror id on success, otherwise -1.
  static int MirrorID(int codec_id);

  // Create memory/instance for storing codec state.
  // Input:
  //   [codec_inst] - information about codec. Only name of codec, "plname", is
  //                  used in this function.
  static ACMGenericCodec* CreateCodecInstance(const CodecInst* codec_inst);

  // Checks if the bitrate is valid for the codec.
  // Input:
  //   [codec_id] - number that specifies codec's position in the database.
  //   [rate] - bitrate to check.
  //   [frame_size_samples] - (used for iLBC) specifies which frame size to go
  //                          with the rate.
  static bool IsRateValid(int codec_id, int rate);
  static bool IsISACRateValid(int rate);
  static bool IsILBCRateValid(int rate, int frame_size_samples);
  static bool IsAMRRateValid(int rate);
  static bool IsAMRwbRateValid(int rate);
  static bool IsG7291RateValid(int rate);
  static bool IsSpeexRateValid(int rate);

  // Check if the payload type is valid, meaning that it is in the valid range
  // of 0 to 127.
  // Input:
  //   [payload_type] - payload type.
  static bool ValidPayloadType(int payload_type);

  // Databases with information about the supported codecs
  // database_ - stored information about all codecs: payload type, name,
  //             sampling frequency, packet size in samples, default channel
  //             support, and default rate.
  // codec_settings_ - stored codec settings: number of allowed packet sizes,
  //                   a vector with the allowed packet sizes, basic block
  //                   samples, and max number of channels that are supported.
  // neteq_decoders_ - list of supported decoders in NetEQ.
  static const CodecInst database_[kMaxNumCodecs];
  static const CodecSettings codec_settings_[kMaxNumCodecs];
  static WebRtcNetEQDecoder neteq_decoders_[kMaxNumCodecs];
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_AUDIO_CODING_MAIN_SOURCE_ACM_CODEC_DATABASE_H_
