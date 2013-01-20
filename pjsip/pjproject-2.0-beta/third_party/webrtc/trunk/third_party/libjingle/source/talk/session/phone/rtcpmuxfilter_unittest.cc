// libjingle
// Copyright 2011 Google Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//  3. The name of the author may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "talk/session/phone/rtcpmuxfilter.h"

#include "talk/base/gunit.h"
#include "talk/session/phone/testutils.h"

TEST(RtcpMuxFilterTest, DemuxRtcpSender) {
  cricket::RtcpMuxFilter filter;
  const char data[] = { 0, 73, 0, 0 };
  const int len = 4;

  // Init state - refuse to demux
  EXPECT_FALSE(filter.DemuxRtcp(data, len));
  // After sent offer, demux should be enabled
  filter.SetOffer(true, cricket::CS_LOCAL);
  EXPECT_TRUE(filter.DemuxRtcp(data, len));
  // Remote accepted, demux should be enabled
  filter.SetAnswer(true, cricket::CS_REMOTE);
  EXPECT_TRUE(filter.DemuxRtcp(data, len));
}

TEST(RtcpMuxFilterTest, DemuxRtcpReceiver) {
  cricket::RtcpMuxFilter filter;
  const char data[] = { 0, 73, 0, 0 };
  const int len = 4;

  // Init state - refuse to demux
  EXPECT_FALSE(filter.DemuxRtcp(data, len));
  // After received offer, demux should not be enabled
  filter.SetOffer(true, cricket::CS_REMOTE);
  EXPECT_FALSE(filter.DemuxRtcp(data, len));
  // We accept, demux is now enabled.
  filter.SetAnswer(true, cricket::CS_LOCAL);
  EXPECT_TRUE(filter.DemuxRtcp(data, len));
}

TEST(RtcpMuxFilterTest, IsActiveSender) {
  cricket::RtcpMuxFilter filter;
  // Init state - not active.
  EXPECT_FALSE(filter.IsActive());
  // After sent offer, demux should not be active
  filter.SetOffer(true, cricket::CS_LOCAL);
  EXPECT_FALSE(filter.IsActive());
  // Remote accepted, filter is now active
  filter.SetAnswer(true, cricket::CS_REMOTE);
  EXPECT_TRUE(filter.IsActive());
}

TEST(RtcpMuxFilterTest, IsActiveReceiver) {
  cricket::RtcpMuxFilter filter;
  // Init state - not active.
  EXPECT_FALSE(filter.IsActive());
  // After received offer, demux should not be active
  filter.SetOffer(true, cricket::CS_REMOTE);
  EXPECT_FALSE(filter.IsActive());
  // We accept, filter is now active
  filter.SetAnswer(true, cricket::CS_LOCAL);
  EXPECT_TRUE(filter.IsActive());
}
