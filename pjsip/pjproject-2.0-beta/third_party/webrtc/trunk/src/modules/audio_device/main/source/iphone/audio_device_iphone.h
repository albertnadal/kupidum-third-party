/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_IPHONE_H
#define WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_IPHONE_H

#include "engine_configurations.h"
#include "audio_device_generic.h"
#include "critical_section_wrapper.h"

#include <AudioToolbox/AudioConverter.h>
#include <AudioUnit/AudioUnit.h>
#include <mach/semaphore.h>

struct PaUtilRingBuffer;

namespace webrtc
{
class EventWrapper;
class ThreadWrapper;

const WebRtc_UWord32 N_REC_SAMPLES_PER_SEC = WEBRTC_AUDIOPROCESSING_SAMPLERATE;
const WebRtc_UWord32 N_PLAY_SAMPLES_PER_SEC = WEBRTC_AUDIOPROCESSING_SAMPLERATE;

const WebRtc_UWord32 N_REC_CHANNELS = 1; // default is mono recording
const WebRtc_UWord32 N_PLAY_CHANNELS = 1; // default is mono playout
const WebRtc_UWord32 N_DEVICE_CHANNELS = 8;

const WebRtc_UWord32 ENGINE_REC_BUF_SIZE_IN_SAMPLES = (N_REC_SAMPLES_PER_SEC
    / 100);
const WebRtc_UWord32 ENGINE_PLAY_BUF_SIZE_IN_SAMPLES = (N_PLAY_SAMPLES_PER_SEC
    / 100);

enum
{
    N_BLOCKS_IO = 2
};
enum
{
    N_BUFFERS_IN = 10
};
enum
{
    N_BUFFERS_OUT = 3
}; // Must be at least N_BLOCKS_IO

const WebRtc_UWord32 TIMER_PERIOD_MS = (2 * 10 * N_BLOCKS_IO * 1000000);

const WebRtc_UWord32 REC_BUF_SIZE_IN_SAMPLES = (ENGINE_REC_BUF_SIZE_IN_SAMPLES
    * N_DEVICE_CHANNELS * N_BUFFERS_IN);
const WebRtc_UWord32 PLAY_BUF_SIZE_IN_SAMPLES =
    (ENGINE_PLAY_BUF_SIZE_IN_SAMPLES * N_PLAY_CHANNELS * N_BUFFERS_OUT);

#define AudioDeviceID SInt32
#define kAudioObjectUnknown -1
    
class AudioDeviceIPhone: public AudioDeviceGeneric
{
public:
    AudioDeviceIPhone(const WebRtc_Word32 id);
    virtual ~AudioDeviceIPhone();

    // Retrieve the currently utilized audio layer
    virtual WebRtc_Word32
        ActiveAudioLayer(AudioDeviceModule::AudioLayer& audioLayer) const;

    // Main initializaton and termination
    virtual WebRtc_Word32 Init();
    virtual WebRtc_Word32 Terminate();
    virtual bool Initialized() const;

    // Device enumeration
    virtual WebRtc_Word16 PlayoutDevices();
    virtual WebRtc_Word16 RecordingDevices();
    virtual WebRtc_Word32 PlayoutDeviceName(
        WebRtc_UWord16 index,
        WebRtc_Word8 name[kAdmMaxDeviceNameSize],
        WebRtc_Word8 guid[kAdmMaxGuidSize]);
    virtual WebRtc_Word32 RecordingDeviceName(
        WebRtc_UWord16 index,
        WebRtc_Word8 name[kAdmMaxDeviceNameSize],
        WebRtc_Word8 guid[kAdmMaxGuidSize]);

    // Device selection
    virtual WebRtc_Word32 SetPlayoutDevice(WebRtc_UWord16 index);
    virtual WebRtc_Word32 SetPlayoutDevice(
        AudioDeviceModule::WindowsDeviceType device);
    virtual WebRtc_Word32 SetRecordingDevice(WebRtc_UWord16 index);
    virtual WebRtc_Word32 SetRecordingDevice(
        AudioDeviceModule::WindowsDeviceType device);

    // Audio transport initialization
    virtual WebRtc_Word32 PlayoutIsAvailable(bool& available);
    virtual WebRtc_Word32 InitPlayout();
    virtual bool PlayoutIsInitialized() const;
    virtual WebRtc_Word32 RecordingIsAvailable(bool& available);
    virtual WebRtc_Word32 InitRecording();
    virtual bool RecordingIsInitialized() const;

    // Audio transport control
    virtual WebRtc_Word32 StartPlayout();
    virtual WebRtc_Word32 StopPlayout();
    virtual bool Playing() const;
    virtual WebRtc_Word32 StartRecording();
    virtual WebRtc_Word32 StopRecording();
    virtual bool Recording() const;

    // Microphone Automatic Gain Control (AGC)
    virtual WebRtc_Word32 SetAGC(bool enable);
    virtual bool AGC() const;

    // Volume control based on the Windows Wave API (Windows only)
    virtual WebRtc_Word32 SetWaveOutVolume(WebRtc_UWord16 volumeLeft,
                                           WebRtc_UWord16 volumeRight);
    virtual WebRtc_Word32 WaveOutVolume(WebRtc_UWord16& volumeLeft,
                                        WebRtc_UWord16& volumeRight) const;

    // Audio mixer initialization
    virtual WebRtc_Word32 SpeakerIsAvailable(bool& available);
    virtual WebRtc_Word32 InitSpeaker();
    virtual bool SpeakerIsInitialized() const;
    virtual WebRtc_Word32 MicrophoneIsAvailable(bool& available);
    virtual WebRtc_Word32 InitMicrophone();
    virtual bool MicrophoneIsInitialized() const;

    // Speaker volume controls
    virtual WebRtc_Word32 SpeakerVolumeIsAvailable(bool& available);
    virtual WebRtc_Word32 SetSpeakerVolume(WebRtc_UWord32 volume);
    virtual WebRtc_Word32 SpeakerVolume(WebRtc_UWord32& volume) const;
    virtual WebRtc_Word32 MaxSpeakerVolume(WebRtc_UWord32& maxVolume) const;
    virtual WebRtc_Word32 MinSpeakerVolume(WebRtc_UWord32& minVolume) const;
    virtual WebRtc_Word32 SpeakerVolumeStepSize(WebRtc_UWord16& stepSize) const;

    // Microphone volume controls
    virtual WebRtc_Word32 MicrophoneVolumeIsAvailable(bool& available);
    virtual WebRtc_Word32 SetMicrophoneVolume(WebRtc_UWord32 volume);
    virtual WebRtc_Word32 MicrophoneVolume(WebRtc_UWord32& volume) const;
    virtual WebRtc_Word32 MaxMicrophoneVolume(WebRtc_UWord32& maxVolume) const;
    virtual WebRtc_Word32 MinMicrophoneVolume(WebRtc_UWord32& minVolume) const;
    virtual WebRtc_Word32
        MicrophoneVolumeStepSize(WebRtc_UWord16& stepSize) const;

    // Microphone mute control
    virtual WebRtc_Word32 MicrophoneMuteIsAvailable(bool& available);
    virtual WebRtc_Word32 SetMicrophoneMute(bool enable);
    virtual WebRtc_Word32 MicrophoneMute(bool& enabled) const;

    // Speaker mute control
    virtual WebRtc_Word32 SpeakerMuteIsAvailable(bool& available);
    virtual WebRtc_Word32 SetSpeakerMute(bool enable);
    virtual WebRtc_Word32 SpeakerMute(bool& enabled) const;

    // Microphone boost control
    virtual WebRtc_Word32 MicrophoneBoostIsAvailable(bool& available);
    virtual WebRtc_Word32 SetMicrophoneBoost(bool enable);
    virtual WebRtc_Word32 MicrophoneBoost(bool& enabled) const;

    // Stereo support
    virtual WebRtc_Word32 StereoPlayoutIsAvailable(bool& available);
    virtual WebRtc_Word32 SetStereoPlayout(bool enable);
    virtual WebRtc_Word32 StereoPlayout(bool& enabled) const;
    virtual WebRtc_Word32 StereoRecordingIsAvailable(bool& available);
    virtual WebRtc_Word32 SetStereoRecording(bool enable);
    virtual WebRtc_Word32 StereoRecording(bool& enabled) const;

    // Delay information and control
    virtual WebRtc_Word32
        SetPlayoutBuffer(const AudioDeviceModule::BufferType type,
                         WebRtc_UWord16 sizeMS);
    virtual WebRtc_Word32 PlayoutBuffer(AudioDeviceModule::BufferType& type,
                                        WebRtc_UWord16& sizeMS) const;
    virtual WebRtc_Word32 PlayoutDelay(WebRtc_UWord16& delayMS) const;
    virtual WebRtc_Word32 RecordingDelay(WebRtc_UWord16& delayMS) const;

    // CPU load
    virtual WebRtc_Word32 CPULoad(WebRtc_UWord16& load) const;

public:
    virtual bool PlayoutWarning() const;
    virtual bool PlayoutError() const;
    virtual bool RecordingWarning() const;
    virtual bool RecordingError() const;
    virtual void ClearPlayoutWarning();
    virtual void ClearPlayoutError();
    virtual void ClearRecordingWarning();
    virtual void ClearRecordingError();

public:
    virtual void AttachAudioBuffer(AudioDeviceBuffer* audioBuffer);

private:
    void Lock()
    {
        _critSect.Enter();
    }
    ;
    void UnLock()
    {
        _critSect.Leave();
    }
    ;
    WebRtc_Word32 Id()
    {
        return _id;
    }

    static void AtomicSet32(int32_t* theValue, int32_t newValue);
    static int32_t AtomicGet32(int32_t* theValue);

    static void logCAMsg(const TraceLevel level,
                         const TraceModule module,
                         const WebRtc_Word32 id, const char *msg,
                         const char *err);
/*
    WebRtc_Word32 GetNumberDevices(const AudioObjectPropertyScope scope,
                                   AudioDeviceID scopedDeviceIds[],
                                   const WebRtc_UWord32 deviceListLength);

    WebRtc_Word32 GetDeviceName(const AudioObjectPropertyScope scope,
                                const WebRtc_UWord16 index, char* name);

    WebRtc_Word32 InitDevice(WebRtc_UWord16 userDeviceIndex,
                             AudioDeviceID& deviceId, bool isInput);
    
    static OSStatus
        objectListenerProc(AudioObjectID objectId, UInt32 numberAddresses,
                           const AudioObjectPropertyAddress addresses[],
                           void* clientData);

    OSStatus
        implObjectListenerProc(AudioObjectID objectId, UInt32 numberAddresses,
                               const AudioObjectPropertyAddress addresses[]);

    WebRtc_Word32 HandleDeviceChange();

    WebRtc_Word32
        HandleStreamFormatChange(AudioObjectID objectId,
                                 AudioObjectPropertyAddress propertyAddress);

    WebRtc_Word32
        HandleDataSourceChange(AudioObjectID objectId,
                               AudioObjectPropertyAddress propertyAddress);

    WebRtc_Word32
        HandleProcessorOverload(AudioObjectPropertyAddress propertyAddress);
*/
    
    static OSStatus
    outConverterProc(AudioConverterRef audioConverter,
                     UInt32 *numberDataPackets, AudioBufferList *data,
                     AudioStreamPacketDescription **dataPacketDescription,
                     void *userData);
    
    static OSStatus
    inConverterProc(AudioConverterRef audioConverter,
                    UInt32 *numberDataPackets, AudioBufferList *data,
                    AudioStreamPacketDescription **dataPacketDescription,
                    void *inUserData);
    
    OSStatus implOutConverterProc(UInt32 *numberDataPackets,
                                  AudioBufferList *data);
    
    OSStatus implInConverterProc(UInt32 *numberDataPackets,
                                 AudioBufferList *data);
    
    static OSStatus inDeviceIOProc(void                        *inRefCon,
                                   AudioUnitRenderActionFlags  *ioActionFlags,
                                   const AudioTimeStamp        *inTimeStamp,
                                   UInt32                      inBusNumber,
                                   UInt32                      inNumberFrames,
                                   AudioBufferList             *ioData);
    
    static OSStatus outDeviceIOProc(void                        *inRefCon,
                                    AudioUnitRenderActionFlags  *ioActionFlags,
                                    const AudioTimeStamp        *inTimeStamp,
                                    UInt32                      inBusNumber,
                                    UInt32                      inNumberFrames,
                                    AudioBufferList             *ioData);
    
    OSStatus implInDeviceIOProc(const AudioBufferList *inputData,
                                const AudioTimeStamp *inputTime);
    
    OSStatus implOutDeviceIOProc(AudioBufferList *outputData,
                                 const AudioTimeStamp *outputTime);
    
    static bool RunCapture(void*);
    static bool RunRender(void*);
    bool CaptureWorkerThread();
    bool RenderWorkerThread();
    
private:
    AudioDeviceBuffer* _ptrAudioBuffer;

    CriticalSectionWrapper& _critSect;

    EventWrapper& _stopEventRec;
    EventWrapper& _stopEvent;

    ThreadWrapper* _captureWorkerThread;
    ThreadWrapper* _renderWorkerThread;
    WebRtc_UWord32 _captureWorkerThreadId;
    WebRtc_UWord32 _renderWorkerThreadId;

    WebRtc_Word32 _id;

    WebRtc_UWord16 _inputDeviceIndex;
    WebRtc_UWord16 _outputDeviceIndex;
    AudioDeviceID _inputDeviceID;
    AudioDeviceID _outputDeviceID;

    AudioComponentInstance _audioUnit;
    bool _inputDeviceIsSpecified;
    bool _outputDeviceIsSpecified;

    WebRtc_UWord8 _recChannels;
    WebRtc_UWord8 _playChannels;

    SInt16* _captureBufData;
    SInt16* _renderBufData;
    
    SInt16 _captureData[REC_BUF_SIZE_IN_SAMPLES];
    SInt16 _renderConvertData[PLAY_BUF_SIZE_IN_SAMPLES];

    AudioDeviceModule::BufferType _playBufType;

private:
    bool _initialized;
    bool _isShutDown;
    bool _recording;
    bool _playing;
    bool _recIsInitialized;
    bool _playIsInitialized;
    bool _startRec;
    bool _stopRec;
    bool _stopPlay;
    bool _AGC;

    // Atomically set varaibles
    int32_t _renderDeviceIsAlive;
    int32_t _captureDeviceIsAlive;

    bool _twoDevices;
    bool _doStop; // For play if not shared device or play+rec if shared device
    bool _doStopRec; // For rec if not shared device
    bool _stereoRender;
    bool _stereoRenderRequested;

    AudioConverterRef _captureConverter;
    AudioConverterRef _renderConverter;

    AudioStreamBasicDescription _outStreamFormat;
    AudioStreamBasicDescription _outDesiredFormat;
    AudioStreamBasicDescription _inStreamFormat;
    AudioStreamBasicDescription _inDesiredFormat;

    WebRtc_UWord32 _captureLatencyUs;
    WebRtc_UWord32 _renderLatencyUs;

    // Atomically set variables
    mutable int32_t _captureDelayUs;
    mutable int32_t _renderDelayUs;

    WebRtc_Word32 _renderDelayOffsetSamples;
private:
    WebRtc_UWord16 _playBufDelay; // playback delay
    WebRtc_UWord16 _playBufDelayFixed; // fixed playback delay

    WebRtc_UWord16 _playWarning;
    WebRtc_UWord16 _playError;
    WebRtc_UWord16 _recWarning;
    WebRtc_UWord16 _recError;

    PaUtilRingBuffer* _paCaptureBuffer;
    PaUtilRingBuffer* _paRenderBuffer;

    semaphore_t _renderSemaphore;
    semaphore_t _captureSemaphore;

    WebRtc_UWord32 _captureBufSizeSamples;
    WebRtc_UWord32 _renderBufSizeSamples;
};

} //  namespace webrtc

#endif  // MODULES_AUDIO_DEVICE_MAIN_SOURCE_MAC_AUDIO_DEVICE_IPHONE_H_
