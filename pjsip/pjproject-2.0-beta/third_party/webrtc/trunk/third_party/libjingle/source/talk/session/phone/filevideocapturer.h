// libjingle
// Copyright 2004--2005, Google Inc.
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
//
// This file contains two classes, VideoRecorder and FileVideoCapturer.
// VideoRecorder records the captured frames into a file. The file stores a
// sequence of captured frames; each frame has a header defined in struct
// CapturedFrame, followed by the frame data.
//
// FileVideoCapturer, a subclass of VideoCapturer, is a simulated video capturer
// that periodically reads images from a previously recorded file.

#ifndef TALK_SESSION_PHONE_FILEVIDEOCAPTURER_H_
#define TALK_SESSION_PHONE_FILEVIDEOCAPTURER_H_

#include <string>
#include <vector>

#include "talk/base/stream.h"
#include "talk/session/phone/videocapturer.h"

namespace talk_base {
class FileStream;
}

namespace cricket {

// Utility class to record the frames captured by a video capturer into a file.
class VideoRecorder {
 public:
  VideoRecorder() {}
  ~VideoRecorder() { Stop(); }

  // Start the recorder by opening the specified file. Return true if the file
  // is opened successfully. write_header should normally be true; false means
  // write raw frame pixel data to file without any headers.
  bool Start(const std::string& filename, bool write_header);
  // Stop the recorder by closing the file.
  void Stop();
  // Record a video frame to the file. Return true if the frame is written to
  // the file successfully. This method needs to be called after Start() and
  // before Stop().
  bool RecordFrame(const CapturedFrame& frame);

 private:
  talk_base::FileStream video_file_;
  bool write_header_;

  DISALLOW_COPY_AND_ASSIGN(VideoRecorder);
};

// Simulated video capturer that periodically reads frames from a file.
class FileVideoCapturer : public VideoCapturer {
 public:
  FileVideoCapturer();
  virtual ~FileVideoCapturer();

  // Set how many times to repeat reading the file. Repeat forever if the
  // parameter is talk_base::kForever(-1); no repeat if the parameter is 0 or
  // less than -1.
  void set_repeat(int repeat) { repeat_ = repeat; }

  // If ignore_framerate is true, file is read as quickly as possible. If
  // false, read rate is controlled by the timestamps in the video file
  // (thus simulating camera capture). Default value set to false.
  void set_ignore_framerate(bool ignore_framerate) {
    ignore_framerate_ = ignore_framerate;
  }

  bool Init(const std::string& filename);

  // Override virtual methods of parent class VideoCapturer.
  virtual CaptureResult Start(const VideoFormat& capture_format);
  virtual void Stop();
  virtual bool IsRunning();

 protected:
  // Override virtual methods of parent class VideoCapturer.
  virtual bool GetPreferredFourccs(std::vector<uint32>* fourccs);

  // Read the frame header from the file stream, video_file_.
  talk_base::StreamResult ReadFrameHeader(CapturedFrame* frame);

  // Read a frame and determine how long to wait for the next frame. If the
  // frame is read successfully, Set the output parameter, wait_time_ms and
  // return true. Otherwise, do not change wait_time_ms and return false.
  bool ReadFrame(bool first_frame, int* wait_time_ms);

  // Return the CapturedFrame - useful for extracting contents after reading
  // a frame. Should be used only while still reading a file (i.e. only while
  // the CapturedFrame object still exists).
  const CapturedFrame* frame() const {
    return &captured_frame_;
  }

 private:
  class FileReadThread;  // Forward declaration, defined in .cc.

  talk_base::FileStream video_file_;
  CapturedFrame captured_frame_;
  // The number of bytes allocated buffer for captured_frame_.data.
  uint32 frame_buffer_size_;
  FileReadThread* file_read_thread_;
  int repeat_;  // How many times to repeat the file.
  int64 start_time_ns_;  // Time when the file video capturer starts.
  int64 last_frame_timestamp_ns_;  // Timestamp of last read frame.
  bool ignore_framerate_;

  DISALLOW_COPY_AND_ASSIGN(FileVideoCapturer);
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_FILEVIDEOCAPTURER_H_
