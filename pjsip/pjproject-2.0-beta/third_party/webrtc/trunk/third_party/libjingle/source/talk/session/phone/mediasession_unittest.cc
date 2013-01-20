/*
 * libjingle
 * Copyright 2004--2011, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>
#include <vector>

#include "talk/base/gunit.h"
#include "talk/p2p/base/constants.h"
#include "talk/session/phone/codec.h"
#include "talk/session/phone/mediasession.h"
#include "talk/session/phone/srtpfilter.h"
#include "talk/session/phone/testutils.h"

#ifdef HAVE_SRTP
#define ASSERT_CRYPTO(cd, r, s, cs) \
    ASSERT_EQ(r, cd->crypto_required()); \
    ASSERT_EQ(s, cd->cryptos().size()); \
    ASSERT_EQ(std::string(cs), cd->cryptos()[0].cipher_suite)
#else
#define ASSERT_CRYPTO(c, r, s, cs) \
  ASSERT_EQ(false, cd->crypto_required()); \
  ASSERT_EQ(0U, cd->cryptos().size());
#endif

using cricket::MediaSessionDescriptionFactory;
using cricket::MediaSessionOptions;
using cricket::MediaType;
using cricket::SessionDescription;
using cricket::SsrcGroup;
using cricket::StreamParams;
using cricket::StreamParamsVec;
using cricket::ContentInfo;
using cricket::CryptoParamsVec;
using cricket::AudioContentDescription;
using cricket::MediaContentDescription;
using cricket::VideoContentDescription;
using cricket::kAutoBandwidth;
using cricket::AudioCodec;
using cricket::VideoCodec;
using cricket::NS_JINGLE_RTP;
using cricket::MEDIA_TYPE_AUDIO;
using cricket::MEDIA_TYPE_VIDEO;
using cricket::SEC_ENABLED;
using cricket::CS_AES_CM_128_HMAC_SHA1_32;
using cricket::CS_AES_CM_128_HMAC_SHA1_80;

static const AudioCodec kAudioCodecs1[] = {
  AudioCodec(103, "ISAC",   16000, -1,    1, 5),
  AudioCodec(102, "iLBC",   8000,  13300, 1, 4),
  AudioCodec(0,   "PCMU",   8000,  64000, 1, 3),
  AudioCodec(8,   "PCMA",   8000,  64000, 1, 2),
  AudioCodec(117, "red",    8000,  0,     1, 1),
};

static const AudioCodec kAudioCodecs2[] = {
  AudioCodec(126, "speex",  16000, 22000, 1, 3),
  AudioCodec(127, "iLBC",   8000,  13300, 1, 2),
  AudioCodec(0,   "PCMU",   8000,  64000, 1, 1),
};

static const AudioCodec kAudioCodecsAnswer[] = {
  AudioCodec(102, "iLBC",   8000,  13300, 1, 2),
  AudioCodec(0,   "PCMU",   8000,  64000, 1, 1),
};

static const VideoCodec kVideoCodecs1[] = {
  VideoCodec(96, "H264-SVC", 320, 200, 30, 2),
  VideoCodec(97, "H264", 320, 200, 30, 1)
};

static const VideoCodec kVideoCodecs2[] = {
  VideoCodec(126, "H264", 320, 200, 30, 2),
  VideoCodec(127, "H263", 320, 200, 30, 1)
};

static const VideoCodec kVideoCodecsAnswer[] = {
  VideoCodec(97, "H264", 320, 200, 30, 2)
};

static const uint32 kSimulcastParamsSsrc[] = {10, 11, 20, 21, 30, 31};
static const uint32 kSimSsrc[] = {10, 20, 30};
static const uint32 kFec1Ssrc[] = {10, 11};
static const uint32 kFec2Ssrc[] = {20, 21};
static const uint32 kFec3Ssrc[] = {30, 31};

static const char kMediaStream1[] = "stream_1";
static const char kMediaStream2[] = "stream_2";
static const char kVideoTrack1[] = "video_1";
static const char kVideoTrack2[] = "video_2";
static const char kAudioTrack1[] = "audio_1";
static const char kAudioTrack2[] = "audio_2";
static const char kAudioTrack3[] = "audio_3";

class MediaSessionDescriptionFactoryTest : public testing::Test {
 public:
  MediaSessionDescriptionFactoryTest() {
    f1_.set_audio_codecs(MAKE_VECTOR(kAudioCodecs1));
    f1_.set_video_codecs(MAKE_VECTOR(kVideoCodecs1));
    f2_.set_audio_codecs(MAKE_VECTOR(kAudioCodecs2));
    f2_.set_video_codecs(MAKE_VECTOR(kVideoCodecs2));
  }

  // Create a video StreamParamsVec object with:
  // - one video stream with 3 simulcast streams and FEC,
  StreamParamsVec CreateComplexVideoStreamParamsVec() {
    SsrcGroup sim_group("SIM", MAKE_VECTOR(kSimSsrc));
    SsrcGroup fec_group1("FEC", MAKE_VECTOR(kFec1Ssrc));
    SsrcGroup fec_group2("FEC", MAKE_VECTOR(kFec2Ssrc));
    SsrcGroup fec_group3("FEC", MAKE_VECTOR(kFec3Ssrc));

    std::vector<SsrcGroup> ssrc_groups;
    ssrc_groups.push_back(sim_group);
    ssrc_groups.push_back(fec_group1);
    ssrc_groups.push_back(fec_group2);
    ssrc_groups.push_back(fec_group3);

    StreamParams simulcast_params;
    simulcast_params.name = kVideoTrack1;
    simulcast_params.ssrcs = MAKE_VECTOR(kSimulcastParamsSsrc);
    simulcast_params.ssrc_groups = ssrc_groups;
    simulcast_params.cname = "Video_SIM_FEC";
    simulcast_params.sync_label = kMediaStream1;

    StreamParamsVec video_streams;
    video_streams.push_back(simulcast_params);

    return video_streams;
  }
  bool CompareCryptoParams(const CryptoParamsVec& c1,
                           const CryptoParamsVec& c2) {
    if (c1.size() != c2.size())
      return false;
    for (size_t i = 0; i < c1.size(); ++i)
      if (c1[i].tag != c2[i].tag || c1[i].cipher_suite != c2[i].cipher_suite ||
          c1[i].key_params != c2[i].key_params ||
          c1[i].session_params != c2[i].session_params)
        return false;
    return true;
  }

 protected:
  const MediaContentDescription*  GetMediaDescription(
      const SessionDescription* sdesc,
      const std::string& content_name) {
    const ContentInfo* content = sdesc->GetContentByName(content_name);
    if (content == NULL) {
      return NULL;
    }
    return static_cast<const MediaContentDescription*>(
        content->description);
  }

  const AudioContentDescription*  GetAudioDescription(
      const SessionDescription* sdesc) {
    return static_cast<const AudioContentDescription*>(
        GetMediaDescription(sdesc, "audio"));
  }

  const VideoContentDescription*  GetVideoDescription(
      const SessionDescription* sdesc) {
    return static_cast<const VideoContentDescription*>(
        GetMediaDescription(sdesc, "video"));
  }

  MediaSessionDescriptionFactory f1_;
  MediaSessionDescriptionFactory f2_;
};

// Create a typical audio offer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateAudioOffer) {
  f1_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(
      f1_.CreateOffer(MediaSessionOptions(), NULL));
  ASSERT_TRUE(offer.get() != NULL);
  const ContentInfo* ac = offer->GetContentByName("audio");
  const ContentInfo* vc = offer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc == NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(f1_.audio_codecs(), acd->codecs());
  EXPECT_NE(0U, acd->first_ssrc());             // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(acd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(acd, false, 2U, CS_AES_CM_128_HMAC_SHA1_32);
}

// Create a typical video offer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateVideoOffer) {
  MediaSessionOptions opts;
  opts.has_video = true;
  f1_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription>
      offer(f1_.CreateOffer(opts, NULL));
  ASSERT_TRUE(offer.get() != NULL);
  const ContentInfo* ac = offer->GetContentByName("audio");
  const ContentInfo* vc = offer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), vc->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* vcd =
      static_cast<const VideoContentDescription*>(vc->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(f1_.audio_codecs(), acd->codecs());
  EXPECT_NE(0U, acd->first_ssrc());             // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(acd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(acd, false, 2U, CS_AES_CM_128_HMAC_SHA1_32);
  EXPECT_EQ(MEDIA_TYPE_VIDEO, vcd->type());
  EXPECT_EQ(f1_.video_codecs(), vcd->codecs());
  EXPECT_NE(0U, vcd->first_ssrc());             // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, vcd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(vcd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);
}

// Create a typical audio answer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateAudioAnswer) {
  f1_.set_secure(SEC_ENABLED);
  f2_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(
      f1_.CreateOffer(MediaSessionOptions(), NULL));
  ASSERT_TRUE(offer.get() != NULL);
  talk_base::scoped_ptr<SessionDescription> answer(
      f2_.CreateAnswer(offer.get(), MediaSessionOptions(), NULL));
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc == NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(MAKE_VECTOR(kAudioCodecsAnswer), acd->codecs());
  EXPECT_NE(0U, acd->first_ssrc());             // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // negotiated auto bw
  EXPECT_TRUE(acd->rtcp_mux());                 // negotiated rtcp-mux
  ASSERT_CRYPTO(acd, false, 1U, CS_AES_CM_128_HMAC_SHA1_32);
}

// Create a typical video answer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateVideoAnswer) {
  MediaSessionOptions opts;
  opts.has_video = true;
  f1_.set_secure(SEC_ENABLED);
  f2_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(f1_.CreateOffer(opts, NULL));
  ASSERT_TRUE(offer.get() != NULL);
  talk_base::scoped_ptr<SessionDescription> answer(
      f2_.CreateAnswer(offer.get(), opts, NULL));
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), vc->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* vcd =
      static_cast<const VideoContentDescription*>(vc->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(MAKE_VECTOR(kAudioCodecsAnswer), acd->codecs());
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // negotiated auto bw
  EXPECT_NE(0U, acd->first_ssrc());             // a random nonzero ssrc
  EXPECT_TRUE(acd->rtcp_mux());                 // negotiated rtcp-mux
  ASSERT_CRYPTO(acd, false, 1U, CS_AES_CM_128_HMAC_SHA1_32);
  EXPECT_EQ(MEDIA_TYPE_VIDEO, vcd->type());
  EXPECT_EQ(MAKE_VECTOR(kVideoCodecsAnswer), vcd->codecs());
  EXPECT_NE(0U, vcd->first_ssrc());             // a random nonzero ssrc
  EXPECT_TRUE(vcd->rtcp_mux());                 // negotiated rtcp-mux
  ASSERT_CRYPTO(vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);
}


// Create a typical video answer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateVideoAnswerRtcpMux) {
  MediaSessionOptions offer_opts;
  MediaSessionOptions answer_opts;
  answer_opts.has_video = true;
  offer_opts.has_video = true;

  talk_base::scoped_ptr<SessionDescription> offer(NULL);
  talk_base::scoped_ptr<SessionDescription> answer(NULL);

  offer_opts.rtcp_mux_enabled = true;
  answer_opts.rtcp_mux_enabled = true;

  offer.reset(f1_.CreateOffer(offer_opts, NULL));
  answer.reset(f2_.CreateAnswer(offer.get(), answer_opts, NULL));
  ASSERT_TRUE(NULL != GetAudioDescription(offer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(offer.get()));
  ASSERT_TRUE(NULL != GetAudioDescription(answer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(answer.get()));
  EXPECT_TRUE(GetAudioDescription(offer.get())->rtcp_mux());
  EXPECT_TRUE(GetVideoDescription(offer.get())->rtcp_mux());
  EXPECT_TRUE(GetAudioDescription(answer.get())->rtcp_mux());
  EXPECT_TRUE(GetVideoDescription(answer.get())->rtcp_mux());

  offer_opts.rtcp_mux_enabled = true;
  answer_opts.rtcp_mux_enabled = false;

  offer.reset(f1_.CreateOffer(offer_opts, NULL));
  answer.reset(f2_.CreateAnswer(offer.get(), answer_opts, NULL));
  ASSERT_TRUE(NULL != GetAudioDescription(offer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(offer.get()));
  ASSERT_TRUE(NULL != GetAudioDescription(answer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(answer.get()));
  EXPECT_TRUE(GetAudioDescription(offer.get())->rtcp_mux());
  EXPECT_TRUE(GetVideoDescription(offer.get())->rtcp_mux());
  EXPECT_FALSE(GetAudioDescription(answer.get())->rtcp_mux());
  EXPECT_FALSE(GetVideoDescription(answer.get())->rtcp_mux());

  offer_opts.rtcp_mux_enabled = false;
  answer_opts.rtcp_mux_enabled = true;

  offer.reset(f1_.CreateOffer(offer_opts, NULL));
  answer.reset(f2_.CreateAnswer(offer.get(), answer_opts, NULL));
  ASSERT_TRUE(NULL != GetAudioDescription(offer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(offer.get()));
  ASSERT_TRUE(NULL != GetAudioDescription(answer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(answer.get()));
  EXPECT_FALSE(GetAudioDescription(offer.get())->rtcp_mux());
  EXPECT_FALSE(GetVideoDescription(offer.get())->rtcp_mux());
  EXPECT_FALSE(GetAudioDescription(answer.get())->rtcp_mux());
  EXPECT_FALSE(GetVideoDescription(answer.get())->rtcp_mux());

  offer_opts.rtcp_mux_enabled = false;
  answer_opts.rtcp_mux_enabled = false;

  offer.reset(f1_.CreateOffer(offer_opts, NULL));
  answer.reset(f2_.CreateAnswer(offer.get(), answer_opts, NULL));
  ASSERT_TRUE(NULL != GetAudioDescription(offer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(offer.get()));
  ASSERT_TRUE(NULL != GetAudioDescription(answer.get()));
  ASSERT_TRUE(NULL != GetVideoDescription(answer.get()));
  EXPECT_FALSE(GetAudioDescription(offer.get())->rtcp_mux());
  EXPECT_FALSE(GetVideoDescription(offer.get())->rtcp_mux());
  EXPECT_FALSE(GetAudioDescription(answer.get())->rtcp_mux());
  EXPECT_FALSE(GetVideoDescription(answer.get())->rtcp_mux());
}

// Create an audio-only answer to a video offer.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateAudioAnswerToVideo) {
  MediaSessionOptions opts;
  opts.has_video = true;
  talk_base::scoped_ptr<SessionDescription>
      offer(f1_.CreateOffer(opts, NULL));
  ASSERT_TRUE(offer.get() != NULL);
  talk_base::scoped_ptr<SessionDescription> answer(
      f2_.CreateAnswer(offer.get(), MediaSessionOptions(), NULL));
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc == NULL);
}

// Create an audio and video offer with:
// - one video track,
// - two audio tracks.
// and ensure it matches what we expect. Also updates the initial offer by
// adding a new video track and replaces one of the audio tracks.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateMultiStreamVideoOffer) {
  MediaSessionOptions opts;
  opts.AddStream(MEDIA_TYPE_VIDEO, kVideoTrack1, kMediaStream1);
  opts.AddStream(MEDIA_TYPE_AUDIO, kAudioTrack1, kMediaStream1);
  opts.AddStream(MEDIA_TYPE_AUDIO, kAudioTrack2, kMediaStream1);

  f1_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(f1_.CreateOffer(opts, NULL));

  ASSERT_TRUE(offer.get() != NULL);
  const ContentInfo* ac = offer->GetContentByName("audio");
  const ContentInfo* vc = offer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* vcd =
      static_cast<const VideoContentDescription*>(vc->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(f1_.audio_codecs(), acd->codecs());

  const StreamParamsVec& audio_streams = acd->streams();
  ASSERT_EQ(2U, audio_streams.size());
  EXPECT_EQ(audio_streams[0].cname , audio_streams[1].cname);
  EXPECT_EQ(kAudioTrack1, audio_streams[0].name);
  ASSERT_EQ(1U, audio_streams[0].ssrcs.size());
  EXPECT_NE(0U, audio_streams[0].ssrcs[0]);
  EXPECT_EQ(kAudioTrack2, audio_streams[1].name);
  ASSERT_EQ(1U, audio_streams[1].ssrcs.size());
  EXPECT_NE(0U, audio_streams[1].ssrcs[0]);

  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(acd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(acd, false, 2U, CS_AES_CM_128_HMAC_SHA1_32);

  EXPECT_EQ(MEDIA_TYPE_VIDEO, vcd->type());
  EXPECT_EQ(f1_.video_codecs(), vcd->codecs());
  ASSERT_CRYPTO(vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);

  const StreamParamsVec& video_streams = vcd->streams();
  ASSERT_EQ(1U, video_streams.size());
  EXPECT_EQ(video_streams[0].cname, audio_streams[0].cname);
  EXPECT_EQ(kVideoTrack1, video_streams[0].name);
  EXPECT_EQ(kAutoBandwidth, vcd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(vcd->rtcp_mux());                 // rtcp-mux defaults on


  // Update the offer. Add a new video track that is not synched to the
  // other tracks and replace audio track 2 with audio track 3.
  opts.AddStream(MEDIA_TYPE_VIDEO, kVideoTrack2, kMediaStream2);
  opts.RemoveStream(MEDIA_TYPE_AUDIO, kAudioTrack2);
  opts.AddStream(MEDIA_TYPE_AUDIO, kAudioTrack3, kMediaStream1);
  talk_base::scoped_ptr<SessionDescription>
      updated_offer(f1_.CreateOffer(opts, offer.get()));

  ASSERT_TRUE(updated_offer.get() != NULL);
  ac = updated_offer->GetContentByName("audio");
  vc = updated_offer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  const AudioContentDescription* updated_acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* updated_vcd =
      static_cast<const VideoContentDescription*>(vc->description);

  EXPECT_EQ(acd->type(), updated_acd->type());
  EXPECT_EQ(acd->codecs(), updated_acd->codecs());
  EXPECT_EQ(vcd->type(), updated_vcd->type());
  EXPECT_EQ(vcd->codecs(), updated_vcd->codecs());
  ASSERT_CRYPTO(updated_acd, false, 2U, CS_AES_CM_128_HMAC_SHA1_32);
  EXPECT_TRUE(CompareCryptoParams(acd->cryptos(), updated_acd->cryptos()));
  ASSERT_CRYPTO(updated_vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);
  EXPECT_TRUE(CompareCryptoParams(vcd->cryptos(), updated_vcd->cryptos()));

  const StreamParamsVec& updated_audio_streams = updated_acd->streams();
  ASSERT_EQ(2U, updated_audio_streams.size());
  EXPECT_EQ(audio_streams[0], updated_audio_streams[0]);
  EXPECT_EQ(kAudioTrack3, updated_audio_streams[1].name);  // New audio track.
  ASSERT_EQ(1U, updated_audio_streams[1].ssrcs.size());
  EXPECT_NE(0U, updated_audio_streams[1].ssrcs[0]);
  EXPECT_EQ(updated_audio_streams[0].cname, updated_audio_streams[1].cname);

  const StreamParamsVec& updated_video_streams = updated_vcd->streams();
  ASSERT_EQ(2U, updated_video_streams.size());
  EXPECT_EQ(video_streams[0], updated_video_streams[0]);
  EXPECT_EQ(kVideoTrack2, updated_video_streams[1].name);
  EXPECT_NE(updated_video_streams[1].cname, updated_video_streams[0].cname);
}

// Create an audio and video answer to a standard video offer with:
// - one video track,
// - two audio tracks.
// and ensure it matches what we expect. Also updates the initial answer by
// adding a new video track and removes one of the audio tracks.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateMultiStreamVideoAnswer) {
  MediaSessionOptions offer_opts;
  offer_opts.has_video = true;
  f1_.set_secure(SEC_ENABLED);
  f2_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(f1_.CreateOffer(offer_opts,
                                                                  NULL));

  MediaSessionOptions opts;
  opts.AddStream(MEDIA_TYPE_VIDEO, kVideoTrack1, kMediaStream1);
  opts.AddStream(MEDIA_TYPE_AUDIO, kAudioTrack1, kMediaStream1);
  opts.AddStream(MEDIA_TYPE_AUDIO, kAudioTrack2, kMediaStream1);

  talk_base::scoped_ptr<SessionDescription>
      answer(f2_.CreateAnswer(offer.get(), opts, NULL));

  ASSERT_TRUE(answer.get() != NULL);
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* vcd =
      static_cast<const VideoContentDescription*>(vc->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(MAKE_VECTOR(kAudioCodecsAnswer), acd->codecs());
  ASSERT_CRYPTO(acd, false, 1U, CS_AES_CM_128_HMAC_SHA1_32);
  ASSERT_CRYPTO(vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);

  const StreamParamsVec& audio_streams = acd->streams();
  ASSERT_EQ(2U, audio_streams.size());
  EXPECT_TRUE(audio_streams[0].cname ==  audio_streams[1].cname);
  EXPECT_EQ(kAudioTrack1, audio_streams[0].name);
  ASSERT_EQ(1U, audio_streams[0].ssrcs.size());
  EXPECT_NE(0U, audio_streams[0].ssrcs[0]);
  EXPECT_EQ(kAudioTrack2, audio_streams[1].name);
  ASSERT_EQ(1U, audio_streams[1].ssrcs.size());
  EXPECT_NE(0U, audio_streams[1].ssrcs[0]);

  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(acd->rtcp_mux());                 // rtcp-mux defaults on

  EXPECT_EQ(MEDIA_TYPE_VIDEO, vcd->type());
  EXPECT_EQ(MAKE_VECTOR(kVideoCodecsAnswer), vcd->codecs());

  const StreamParamsVec& video_streams = vcd->streams();
  ASSERT_EQ(1U, video_streams.size());
  EXPECT_EQ(video_streams[0].cname, audio_streams[0].cname);
  EXPECT_EQ(kVideoTrack1, video_streams[0].name);
  EXPECT_EQ(kAutoBandwidth, vcd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(vcd->rtcp_mux());                 // rtcp-mux defaults on

  // Update the answer. Add a new video track that is not synched to the
  // other traacks and remove 1 audio track.
  opts.AddStream(MEDIA_TYPE_VIDEO, kVideoTrack2, kMediaStream2);
  opts.RemoveStream(MEDIA_TYPE_AUDIO, kAudioTrack2);
  talk_base::scoped_ptr<SessionDescription>
      updated_answer(f2_.CreateAnswer(offer.get(), opts, answer.get()));

  ASSERT_TRUE(updated_answer.get() != NULL);
  ac = updated_answer->GetContentByName("audio");
  vc = updated_answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  const AudioContentDescription* updated_acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* updated_vcd =
      static_cast<const VideoContentDescription*>(vc->description);

  ASSERT_CRYPTO(updated_acd, false, 1U, CS_AES_CM_128_HMAC_SHA1_32);
  EXPECT_TRUE(CompareCryptoParams(acd->cryptos(), updated_acd->cryptos()));
  ASSERT_CRYPTO(updated_vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);
  EXPECT_TRUE(CompareCryptoParams(vcd->cryptos(), updated_vcd->cryptos()));

  EXPECT_EQ(acd->type(), updated_acd->type());
  EXPECT_EQ(acd->codecs(), updated_acd->codecs());
  EXPECT_EQ(vcd->type(), updated_vcd->type());
  EXPECT_EQ(vcd->codecs(), updated_vcd->codecs());

  const StreamParamsVec& updated_audio_streams = updated_acd->streams();
  ASSERT_EQ(1U, updated_audio_streams.size());
  EXPECT_TRUE(audio_streams[0] ==  updated_audio_streams[0]);

  const StreamParamsVec& updated_video_streams = updated_vcd->streams();
  ASSERT_EQ(2U, updated_video_streams.size());
  EXPECT_EQ(video_streams[0], updated_video_streams[0]);
  EXPECT_EQ(kVideoTrack2, updated_video_streams[1].name);
  EXPECT_NE(updated_video_streams[1].cname, updated_video_streams[0].cname);
}
