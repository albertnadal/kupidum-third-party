// Copyright 2008 Google Inc. All Rights Reserved,
//
// Author: Ronghua Wu (ronghuawu@google.com)

#include <string>

#include "talk/base/gunit.h"
#include "talk/session/phone/webrtcpassthroughrender.h"
#include "talk/session/phone/testutils.h"

class WebRtcPassthroughRenderTest : public testing::Test {
 public:
  class ExternalRenderer : public webrtc::VideoRenderCallback {
   public:
    ExternalRenderer() : frame_num_(0) {
    }

    virtual ~ExternalRenderer() {
    }

    virtual WebRtc_Word32 RenderFrame(
        const WebRtc_UWord32 stream_id,
        webrtc::VideoFrame& videoFrame) {
      ++frame_num_;
      LOG(INFO) << "RenderFrame stream_id: " << stream_id
                << " frame_num: " << frame_num_;
      return 0;
    }

    int frame_num() const {
      return frame_num_;
    }

   private:
    int frame_num_;
  };

  WebRtcPassthroughRenderTest()
      : renderer_(new cricket::WebRtcPassthroughRender()) {
  }

  ~WebRtcPassthroughRenderTest() {
  }

  webrtc::VideoRenderCallback* AddIncomingRenderStream(int stream_id) {
    return renderer_->AddIncomingRenderStream(stream_id, 0, 0, 0, 0, 0);
  }

  bool HasIncomingRenderStream(int stream_id) {
    return renderer_->HasIncomingRenderStream(stream_id);
  }

  bool DeleteIncomingRenderStream(int stream_id) {
    return (renderer_->DeleteIncomingRenderStream(stream_id) == 0);
  }

  bool AddExternalRenderCallback(int stream_id,
                                 webrtc::VideoRenderCallback* renderer) {
    return (renderer_->AddExternalRenderCallback(stream_id, renderer) == 0);
  }

 private:
  talk_base::scoped_ptr<cricket::WebRtcPassthroughRender> renderer_;
};

TEST_F(WebRtcPassthroughRenderTest, Streams) {
  const int stream_id1 = 1234;
  const int stream_id2 = 5678;
  webrtc::VideoRenderCallback* stream = NULL;
  // Add a new stream
  stream = AddIncomingRenderStream(stream_id1);
  EXPECT_TRUE(stream != NULL);
  EXPECT_TRUE(HasIncomingRenderStream(stream_id1));
  // Tried to add a already existed stream should return null
  stream =AddIncomingRenderStream(stream_id1);
  EXPECT_TRUE(stream == NULL);
  stream = AddIncomingRenderStream(stream_id2);
  EXPECT_TRUE(stream != NULL);
  EXPECT_TRUE(HasIncomingRenderStream(stream_id2));
  // Remove the stream
  EXPECT_TRUE(DeleteIncomingRenderStream(stream_id2));
  EXPECT_TRUE(!HasIncomingRenderStream(stream_id2));
  // Add back the removed stream
  stream = AddIncomingRenderStream(stream_id2);
  EXPECT_TRUE(stream != NULL);
  EXPECT_TRUE(HasIncomingRenderStream(stream_id2));
}

TEST_F(WebRtcPassthroughRenderTest, Renderer) {
  webrtc::VideoFrame frame;
  const int stream_id1 = 1234;
  const int stream_id2 = 5678;
  webrtc::VideoRenderCallback* stream1 = NULL;
  webrtc::VideoRenderCallback* stream2 = NULL;
  // Add two new stream
  stream1 = AddIncomingRenderStream(stream_id1);
  EXPECT_TRUE(stream1 != NULL);
  EXPECT_TRUE(HasIncomingRenderStream(stream_id1));
  stream2 = AddIncomingRenderStream(stream_id2);
  EXPECT_TRUE(stream2 != NULL);
  EXPECT_TRUE(HasIncomingRenderStream(stream_id2));
  // Register the external renderer
  WebRtcPassthroughRenderTest::ExternalRenderer renderer1;
  WebRtcPassthroughRenderTest::ExternalRenderer renderer2;
  AddExternalRenderCallback(stream_id1, &renderer1);
  AddExternalRenderCallback(stream_id2, &renderer2);
  int test_frame_num = 10;
  for (int i = 0; i < test_frame_num; ++i) {
    stream1->RenderFrame(stream_id1, frame);
  }
  EXPECT_EQ(test_frame_num, renderer1.frame_num());
  test_frame_num = 30;
  for (int i = 0; i < test_frame_num; ++i) {
    stream2->RenderFrame(stream_id2, frame);
  }
  EXPECT_EQ(test_frame_num, renderer2.frame_num());
}
