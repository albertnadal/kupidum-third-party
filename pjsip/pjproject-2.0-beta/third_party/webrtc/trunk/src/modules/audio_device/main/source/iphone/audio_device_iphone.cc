/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio_device_utility.h"
#include "audio_device_iphone.h"
#include "audio_device_config.h"

#include "event_wrapper.h"
#include "trace.h"
#include "thread_wrapper.h"

#include <cassert>

#include <sys/sysctl.h>         // sysctlbyname()
#include <mach/mach.h>          // mach_task_self()
#include <mach/mach_time.h>
#include <libkern/OSAtomic.h>   // OSAtomicCompareAndSwap()
#include "portaudio/pa_ringbuffer.h"

#include <AudioToolbox/AudioServices.h>

#define kOutputBus 0
#define kInputBus  1

namespace webrtc
{

#define WEBRTC_CA_RETURN_ON_ERR(expr)                                   \
    do {                                                                \
        err = expr;                                                     \
        if (err != noErr) {                                             \
            logCAMsg(kTraceError, kTraceAudioDevice, _id,               \
                "Error in " #expr, (const char *)&err);                 \
            return -1;                                                  \
        }                                                               \
    } while(0)

#define WEBRTC_CA_LOG_ERR(expr)                                         \
    do {                                                                \
        err = expr;                                                     \
        if (err != noErr) {                                             \
            logCAMsg(kTraceError, kTraceAudioDevice, _id,               \
                "Error in " #expr, (const char *)&err);                 \
        }                                                               \
    } while(0)

#define WEBRTC_CA_LOG_WARN(expr)                                        \
    do {                                                                \
        err = expr;                                                     \
        if (err != noErr) {                                             \
            logCAMsg(kTraceWarning, kTraceAudioDevice, _id,             \
                "Error in " #expr, (const char *)&err);                 \
        }                                                               \
    } while(0)

enum
{
    MaxNumberDevices = 64
};

UInt64 AudioConvertHostTimeToNanos(UInt64 time)
{
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    return (double)time * (double)timebase.numer / (double)timebase.denom / 1e3;
}
    
void AudioDeviceIPhone::AtomicSet32(int32_t* theValue, int32_t newValue)
{
    while (1)
    {
        int32_t oldValue = *theValue;
        if (OSAtomicCompareAndSwap32Barrier(oldValue, newValue, theValue)
            == true)
        {
            return;
        }
    }
}

int32_t AudioDeviceIPhone::AtomicGet32(int32_t* theValue)
{
    while (1)
    {
        WebRtc_Word32 value = *theValue;
        if (OSAtomicCompareAndSwap32Barrier(value, value, theValue) == true)
        {
            return value;
        }
    }
}

// CoreAudio errors are best interpreted as four character strings.
void AudioDeviceIPhone::logCAMsg(const TraceLevel level,
                              const TraceModule module,
                              const WebRtc_Word32 id, const char *msg,
                              const char *err)
{
    assert(msg != NULL);
    assert(err != NULL);

#ifdef WEBRTC_BIG_ENDIAN
    WEBRTC_TRACE(level, module, id, "%s: %.4s", msg, err);
#else
    // We need to flip the characters in this case.
    WEBRTC_TRACE(level, module, id, "%s: %.1s%.1s%.1s%.1s", msg, err + 3, err
        + 2, err + 1, err);
#endif
}
    
//ggb kTraceModuleCall, kTraceAudioDevice

AudioDeviceIPhone::AudioDeviceIPhone(const WebRtc_Word32 id) :
    _ptrAudioBuffer(NULL),
    _critSect(*CriticalSectionWrapper::CreateCriticalSection()),
    _stopEventRec(*EventWrapper::Create()),
    _stopEvent(*EventWrapper::Create()),
    _captureWorkerThread(NULL),
    _renderWorkerThread(NULL),
    _captureWorkerThreadId(0),
    _renderWorkerThreadId(0),
    _id(id),
    _inputDeviceIndex(0),
    _outputDeviceIndex(0),
    _inputDeviceID(kAudioObjectUnknown),
    _outputDeviceID(kAudioObjectUnknown),
    _inputDeviceIsSpecified(false),
    _outputDeviceIsSpecified(false),
    _recChannels(N_REC_CHANNELS),
    _playChannels(N_PLAY_CHANNELS),
    _captureBufData(NULL),
    _renderBufData(NULL),
    _playBufType(AudioDeviceModule::kFixedBufferSize),
    _initialized(false),
    _isShutDown(false),
    _recording(false),
    _playing(false),
    _recIsInitialized(false),
    _playIsInitialized(false),
    _startRec(false),
    _stopRec(false),
    _stopPlay(false),
    _AGC(false),
    _renderDeviceIsAlive(1),
    _captureDeviceIsAlive(1),
    _twoDevices(true),
    _doStop(false),
    _doStopRec(false),
    _captureLatencyUs(0),
    _renderLatencyUs(0),
    _captureDelayUs(0),
    _renderDelayUs(0),
    _renderDelayOffsetSamples(0),
    _playBufDelayFixed(20),
    _playWarning(0),
    _playError(0),
    _recWarning(0),
    _recError(0),
    _paCaptureBuffer(NULL),
    _paRenderBuffer(NULL),
    _captureBufSizeSamples(0),
    _renderBufSizeSamples(0)
{
    WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, id,
                 "%s created", __FUNCTION__);

    assert(&_stopEvent != NULL);
    assert(&_stopEventRec != NULL);

    memset(_renderConvertData, 0, sizeof(_renderConvertData));
    memset(&_outStreamFormat, 0, sizeof(AudioStreamBasicDescription));
    memset(&_outDesiredFormat, 0, sizeof(AudioStreamBasicDescription));
    memset(&_inStreamFormat, 0, sizeof(AudioStreamBasicDescription));
    memset(&_inDesiredFormat, 0, sizeof(AudioStreamBasicDescription));
}


AudioDeviceIPhone::~AudioDeviceIPhone()
{
    WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, _id,
                 "%s destroyed", __FUNCTION__);

    if (!_isShutDown)
    {
        Terminate();
    }

    if (_captureWorkerThread)
    {
        delete _captureWorkerThread;
        _captureWorkerThread = NULL;
    }

    if (_renderWorkerThread)
    {
        delete _renderWorkerThread;
        _renderWorkerThread = NULL;
    }

    if (_paRenderBuffer)
    {
        delete _paRenderBuffer;
        _paRenderBuffer = NULL;
    }

    if (_paCaptureBuffer)
    {
        delete _paCaptureBuffer;
        _paCaptureBuffer = NULL;
    }

    if (_renderBufData)
    {
        delete[] _renderBufData;
        _renderBufData = NULL;
    }

    if (_captureBufData)
    {
        delete[] _captureBufData;
        _captureBufData = NULL;
    }

    kern_return_t kernErr = KERN_SUCCESS;
    kernErr = semaphore_destroy(mach_task_self(), _renderSemaphore);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " semaphore_destroy() error: %d", kernErr);
    }

    kernErr = semaphore_destroy(mach_task_self(), _captureSemaphore);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " semaphore_destroy() error: %d", kernErr);
    }

    delete &_stopEvent;
    delete &_stopEventRec;
    delete &_critSect;
}

// ============================================================================
//                                     API
// ============================================================================

void AudioDeviceIPhone::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    _ptrAudioBuffer = audioBuffer;

    // inform the AudioBuffer about default settings for this implementation
    _ptrAudioBuffer->SetRecordingSampleRate(N_REC_SAMPLES_PER_SEC);
    _ptrAudioBuffer->SetPlayoutSampleRate(N_PLAY_SAMPLES_PER_SEC);
    _ptrAudioBuffer->SetRecordingChannels(N_REC_CHANNELS);
    _ptrAudioBuffer->SetPlayoutChannels(N_PLAY_CHANNELS);
}

WebRtc_Word32 AudioDeviceIPhone::ActiveAudioLayer(
    AudioDeviceModule::AudioLayer& audioLayer) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    audioLayer = AudioDeviceModule::kPlatformDefaultAudio;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::Init()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (_initialized)
    {
        return 0;
    }

    _isShutDown = false;

    // PortAudio ring buffers require an elementCount which is a power of two.
    if (_renderBufData == NULL)
    {
        UInt32 powerOfTwo = 1;
        while (powerOfTwo < PLAY_BUF_SIZE_IN_SAMPLES)
        {
            powerOfTwo <<= 1;
        }
        _renderBufSizeSamples = powerOfTwo;
        _renderBufData = new SInt16[_renderBufSizeSamples];
    }

    if (_paRenderBuffer == NULL)
    {
        _paRenderBuffer = new PaUtilRingBuffer;
        ring_buffer_size_t bufSize = -1;
        bufSize = PaUtil_InitializeRingBuffer(_paRenderBuffer, 
                                              sizeof(SInt16),
                                              _renderBufSizeSamples,
                                              _renderBufData);
        if (bufSize == -1)
        {
            WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice,
                         _id, " PaUtil_InitializeRingBuffer() error");
            return -1;
        }
    }

    if (_captureBufData == NULL)
    {
        UInt32 powerOfTwo = 1;
        while (powerOfTwo < REC_BUF_SIZE_IN_SAMPLES)
        {
            powerOfTwo <<= 1;
        }
        _captureBufSizeSamples = powerOfTwo;
        _captureBufData = new SInt16[_captureBufSizeSamples];
    }

    if (_paCaptureBuffer == NULL)
    {
        _paCaptureBuffer = new PaUtilRingBuffer;
        ring_buffer_size_t bufSize = -1;
        bufSize = PaUtil_InitializeRingBuffer(_paCaptureBuffer,
                                              sizeof(SInt16),
                                              _captureBufSizeSamples,
                                              _captureBufData);
        if (bufSize == -1)
        {
            WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice,
                         _id, " PaUtil_InitializeRingBuffer() error");
            return -1;
        }
    }

    if (_renderWorkerThread == NULL)
    {
        _renderWorkerThread
             = ThreadWrapper::CreateThread(RunRender, this, kRealtimePriority,
                                           "RenderWorkerThread");
        if (_renderWorkerThread == NULL)
        {
            WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice,
                         _id, " Render CreateThread() error");
            return -1;
        }
    }

    if (_captureWorkerThread == NULL)
    {
         _captureWorkerThread
             = ThreadWrapper::CreateThread(RunCapture, this, kRealtimePriority,
                                           "CaptureWorkerThread");
        if (_captureWorkerThread == NULL)
        {
            WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice,
                         _id, " Capture CreateThread() error");
            return -1;
        }
    }

    kern_return_t kernErr = KERN_SUCCESS;
    kernErr = semaphore_create(mach_task_self(), &_renderSemaphore,
                               SYNC_POLICY_FIFO, 0);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice, _id,
                     " semaphore_create() error: %d", kernErr);
        return -1;
    }

    kernErr = semaphore_create(mach_task_self(), &_captureSemaphore,
                               SYNC_POLICY_FIFO, 0);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice, _id,
                     " semaphore_create() error: %d", kernErr);
        return -1;
    }

    OSStatus err = noErr;

    //TODO: Interruption listener
    AudioSessionInitialize(NULL, NULL, NULL, NULL);
    
    UInt32 cat = kAudioSessionCategory_PlayAndRecord;
    err = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
                                  sizeof(cat),
                                  &cat);
    
//    Float32 preferredBufferSize = .010; //10 msecs
//    err = AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration, 
//                                  sizeof(preferredBufferSize), 
//                                  &preferredBufferSize);
    
    AudioComponentDescription desc;
    
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_VoiceProcessingIO;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    // Get component
    AudioComponent inputComponent = AudioComponentFindNext(NULL, &desc);
    
    WEBRTC_CA_RETURN_ON_ERR(AudioComponentInstanceNew(inputComponent, &_audioUnit));
    
    AudioStreamBasicDescription format;
    format.mSampleRate			= N_REC_SAMPLES_PER_SEC;
    format.mFormatID			= kAudioFormatLinearPCM;
    format.mFormatFlags		    = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
#ifdef WEBRTC_BIG_ENDIAN
    format.mFormatFlags        |= kLinearPCMFormatFlagIsBigEndian;
#endif
    format.mFramesPerPacket	    = 1;
    format.mChannelsPerFrame	= N_REC_CHANNELS;
    format.mBitsPerChannel		= sizeof(SInt16) * 8;
    format.mBytesPerPacket		= format.mChannelsPerFrame * sizeof(SInt16);
    format.mBytesPerFrame		= format.mChannelsPerFrame * sizeof(SInt16);
    
    _inDesiredFormat = _outDesiredFormat = format;
    
    // Apply format
    WEBRTC_CA_RETURN_ON_ERR(AudioUnitSetProperty(_audioUnit, 
                                  kAudioUnitProperty_StreamFormat, 
                                  kAudioUnitScope_Output, 
                                  kInputBus, 
                                  &format, 
                                  sizeof(format)));
    
    
    WEBRTC_CA_RETURN_ON_ERR(AudioUnitSetProperty(_audioUnit, 
                                  kAudioUnitProperty_StreamFormat, 
                                  kAudioUnitScope_Input, 
                                  kOutputBus, 
                                  &format, 
                                  sizeof(format)));
    
    UInt32 ioSize = sizeof(_inStreamFormat);
    WEBRTC_CA_RETURN_ON_ERR(AudioUnitGetProperty(_audioUnit, 
                                                 kAudioUnitProperty_StreamFormat, 
                                                 kAudioUnitScope_Input, 
                                                 kOutputBus, 
                                                 &_inStreamFormat, 
                                                 &ioSize));
    
    ioSize = sizeof(_outStreamFormat);
    WEBRTC_CA_RETURN_ON_ERR(AudioUnitGetProperty(_audioUnit, 
                                                 kAudioUnitProperty_StreamFormat, 
                                                 kAudioUnitScope_Output, 
                                                 kInputBus, 
                                                 &_outStreamFormat, 
                                                 &ioSize));
    
    {        
        AURenderCallbackStruct callbackStruct;
        callbackStruct.inputProc = inDeviceIOProc;
        callbackStruct.inputProcRefCon = this;
        err = AudioUnitSetProperty(_audioUnit, 
                                   kAudioOutputUnitProperty_SetInputCallback, 
                                   kAudioUnitScope_Global, 
                                   kInputBus, 
                                   &callbackStruct, 
                                   sizeof(callbackStruct));
        if (err != noErr) {
            
        }
    }
    {
        UInt32 enable = 1;
        err = AudioUnitSetProperty(_audioUnit,
                                   kAudioOutputUnitProperty_EnableIO,
                                   kAudioUnitScope_Input,
                                   kInputBus,
                                   &enable,
                                   sizeof(enable));
        
        if (err != noErr) {
        }
        
        AURenderCallbackStruct callbackStruct;
        callbackStruct.inputProc = outDeviceIOProc;
        callbackStruct.inputProcRefCon = this;
        err = AudioUnitSetProperty(_audioUnit, 
                                   kAudioUnitProperty_SetRenderCallback, 
                                   kAudioUnitScope_Input, 
                                   kOutputBus,
                                   &callbackStruct, 
                                   sizeof(callbackStruct));
        if (err != noErr) {
        }
    }

    WEBRTC_CA_RETURN_ON_ERR(AudioUnitInitialize(_audioUnit));

    _playWarning = 0;
    _playError = 0;
    _recWarning = 0;
    _recError = 0;

    _initialized = true;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::Terminate()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    if (!_initialized)
    {
        return 0;
    }

    if (_recording)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " Recording must be stopped");
        return -1;
    }

    if (_playing)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " Playback must be stopped");
        return -1;
    }

    _critSect.Enter();

    int retVal = 0;

    AudioUnitUninitialize(_audioUnit);
    AudioComponentInstanceDispose(_audioUnit);
    
    _critSect.Leave();

    _isShutDown = true;
    _initialized = false;
    _outputDeviceIsSpecified = false;
    _inputDeviceIsSpecified = false;

    return retVal;
}

bool AudioDeviceIPhone::Initialized() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return (_initialized);
}

WebRtc_Word32 AudioDeviceIPhone::SpeakerIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    // Make an attempt to open up the
    // output mixer corresponding to the currently selected output device.
    //
    if (InitSpeaker() == -1)
    {
        available = false;
        return 0;
    }

    // Given that InitSpeaker was successful, we know that a valid speaker exists
    // 
    available = true;


    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::InitSpeaker()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (_playing)
    {
        return -1;
    }

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    // Make an attempt to open up the
    // input mixer corresponding to the currently selected output device.
    //
    if (InitMicrophone() == -1)
    {
        available = false;
        return 0;
    }

    // Given that InitMicrophone was successful, we know that a valid microphone exists
    // 
    available = true;

    return 0;
}


WebRtc_Word32 AudioDeviceIPhone::InitMicrophone()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (_recording)
    {
        return -1;
    }

    return 0;
}

bool AudioDeviceIPhone::SpeakerIsInitialized() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return _initialized;
}

bool AudioDeviceIPhone::MicrophoneIsInitialized() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return _initialized;
}

WebRtc_Word32 AudioDeviceIPhone::SpeakerVolumeIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetSpeakerVolume(WebRtc_UWord32 volume)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetSpeakerVolume(volume=%u)", volume);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::SpeakerVolume(WebRtc_UWord32& volume) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::SetWaveOutVolume(WebRtc_UWord16 volumeLeft,
                                               WebRtc_UWord16 volumeRight)
{
    WEBRTC_TRACE(
                 kTraceModuleCall,
                 kTraceAudioDevice,
                 _id,
                 "AudioDeviceIPhone::SetWaveOutVolume(volumeLeft=%u, volumeRight=%u)",
                 volumeLeft, volumeRight);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported on this platform");
    
    return -1;
}

WebRtc_Word32
AudioDeviceIPhone::WaveOutVolume(WebRtc_UWord16& volumeLeft,
                              WebRtc_UWord16& volumeRight) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported on this platform");
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MaxSpeakerVolume(WebRtc_UWord32& maxVolume) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MinSpeakerVolume(WebRtc_UWord32& minVolume) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32
AudioDeviceIPhone::SpeakerVolumeStepSize(WebRtc_UWord16& stepSize) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::SpeakerMuteIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetSpeakerMute(bool enable)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetSpeakerMute(enable=%u)", enable);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::SpeakerMute(bool& enabled) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneMuteIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetMicrophoneMute(bool enable)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceWindowsWave::SetMicrophoneMute(enable=%u)", enable);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneMute(bool& enabled) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneBoostIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetMicrophoneBoost(bool enable)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetMicrophoneBoost(enable=%u)", enable);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneBoost(bool& enabled) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::StereoRecordingIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetStereoRecording(bool enable)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetStereoRecording(enable=%u)", enable);
    
    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::StereoRecording(bool& enabled) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    if (_recChannels == 2)
        enabled = true;
    else
        enabled = false;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::StereoPlayoutIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetStereoPlayout(bool enable)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetStereoPlayout(enable=%u)", enable);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::StereoPlayout(bool& enabled) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    if (_playChannels == 2)
        enabled = true;
    else
        enabled = false;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetAGC(bool enable)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetAGC(enable=%d)", enable);

    _AGC = enable;

    return 0;
}

bool AudioDeviceIPhone::AGC() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    return _AGC;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneVolumeIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    available = false;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetMicrophoneVolume(WebRtc_UWord32 volume)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetMicrophoneVolume(volume=%u)", volume);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::MicrophoneVolume(WebRtc_UWord32& volume) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");

    return -1;
}

WebRtc_Word32
AudioDeviceIPhone::MaxMicrophoneVolume(WebRtc_UWord32& maxVolume) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");

    return -1;
}

WebRtc_Word32
AudioDeviceIPhone::MinMicrophoneVolume(WebRtc_UWord32& minVolume) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");

    return -1;
}

WebRtc_Word32
AudioDeviceIPhone::MicrophoneVolumeStepSize(WebRtc_UWord16& stepSize) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word16 AudioDeviceIPhone::PlayoutDevices()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return 1;
}

WebRtc_Word32 AudioDeviceIPhone::SetPlayoutDevice(WebRtc_UWord16 index)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetPlayoutDevice(index=%u)", index);

    if (_playIsInitialized)
    {
        return -1;
    }

    _outputDeviceIndex = index;
    _outputDeviceIsSpecified = true;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetPlayoutDevice(
    AudioDeviceModule::WindowsDeviceType device)
{
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                 "WindowsDeviceType not supported");
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::PlayoutDeviceName(
    WebRtc_UWord16 index,
    WebRtc_Word8 name[kAdmMaxDeviceNameSize],
    WebRtc_Word8 guid[kAdmMaxGuidSize])
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::PlayoutDeviceName(index=%u)", index);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::RecordingDeviceName(
    WebRtc_UWord16 index,
    WebRtc_Word8 name[kAdmMaxDeviceNameSize],
    WebRtc_Word8 guid[kAdmMaxGuidSize])
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::RecordingDeviceName(index=%u)", index);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return -1;
}

WebRtc_Word16 AudioDeviceIPhone::RecordingDevices()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::SetRecordingDevice(WebRtc_UWord16 index)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetRecordingDevice(index=%u)", index);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");
    
    if (_recIsInitialized)
    {
        return -1;
    }

    _inputDeviceIndex = index;
    _inputDeviceIsSpecified = true;

    return 0;
}


WebRtc_Word32
AudioDeviceIPhone::SetRecordingDevice(AudioDeviceModule::WindowsDeviceType /*device*/)
{
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                 "WindowsDeviceType not supported");
    
    return -1;
}

WebRtc_Word32 AudioDeviceIPhone::PlayoutIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    available = true;

    // Try to initialize the playout side
    if (InitPlayout() == -1)
    {
        available = false;
    }

    // We destroy the IOProc created by InitPlayout() in implDeviceIOProc().
    // We must actually start playout here in order to have the IOProc
    // deleted by calling StopPlayout().
    if (StartPlayout() == -1)
    {
        available = false;
    }

    // Cancel effect of initialization
    if (StopPlayout() == -1)
    {
        available = false;
    }

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::RecordingIsAvailable(bool& available)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    available = true;

    // Try to initialize the recording side
    if (InitRecording() == -1)
    {
        available = false;
    }

    // We destroy the IOProc created by InitRecording() in implInDeviceIOProc().
    // We must actually start recording here in order to have the IOProc
    // deleted by calling StopRecording().
    if (StartRecording() == -1)
    {
        available = false;
    }

    // Cancel effect of initialization
    if (StopRecording() == -1)
    {
        available = false;
    }

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::InitPlayout()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (_playing)
    {
        return -1;
    }

    if (!_outputDeviceIsSpecified)
    {
        return -1;
    }

    if (_playIsInitialized)
    {
        return 0;
    }

    // Initialize the speaker (devices might have been added or removed)
    if (InitSpeaker() == -1)
    {
        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                     "  InitSpeaker() failed");
    }

    if (!MicrophoneIsInitialized())
    {
        // Make this call to check if we are using
        // one or two devices (_twoDevices)
        bool available = false;
        if (MicrophoneIsAvailable(available) == -1)
        {
            WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                         "  MicrophoneIsAvailable() failed");
        }
    }

    PaUtil_FlushRingBuffer(_paRenderBuffer);

    _renderDelayOffsetSamples = 0;
    _renderDelayUs = 0;
    _renderLatencyUs = 0;
    _renderDeviceIsAlive = 1;
    _doStop = false;

    // Mark playout side as initialized
    _playIsInitialized = true;

    WEBRTC_TRACE(kTraceInfo, kTraceAudioDevice, _id,
                 "  initial playout status: _renderDelayOffsetSamples=%d,"
                 " _renderDelayUs=%d, _renderLatencyUs=%d",
                 _renderDelayOffsetSamples, _renderDelayUs, _renderLatencyUs);

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::InitRecording()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (_recording)
    {
        return -1;
    }

    if (!_inputDeviceIsSpecified)
    {
        return -1;
    }

    if (_recIsInitialized)
    {
        return 0;
    }

    // Initialize the microphone (devices might have been added or removed)
    if (InitMicrophone() == -1)
    {
        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                     "  InitMicrophone() failed");
    }

    if (!SpeakerIsInitialized())
    {
        // Make this call to check if we are using
        // one or two devices (_twoDevices)
        bool available = false;
        if (SpeakerIsAvailable(available) == -1)
        {
            WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                         "  SpeakerIsAvailable() failed");
        }
    }
    
    PaUtil_FlushRingBuffer(_paCaptureBuffer);

    _captureDelayUs = 0;
    _captureLatencyUs = 0;
    _captureDeviceIsAlive = 1;
    _doStopRec = false;

    // Mark recording side as initialized
    _recIsInitialized = true;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::StartRecording()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (!_recIsInitialized)
    {
        return -1;
    }

    if (_recording)
    {
        return 0;
    }

    if (!_initialized)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " Recording worker thread has not been started");
        return -1;
    }

    OSStatus err = noErr;

    WEBRTC_CA_RETURN_ON_ERR(AudioConverterNew(&_inStreamFormat, &_inDesiredFormat,
                                              &_captureConverter));
    
    unsigned int threadID(0);
    if (_captureWorkerThread != NULL)
    {
        _captureWorkerThread->Start(threadID);
    }
    _captureWorkerThreadId = threadID;

    if (!_playing) {
        AudioSessionSetActive(true);
        OSStatus err = noErr;
        err = AudioOutputUnitStart(_audioUnit);
    }

    _recording = true;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::StopRecording()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (!_recIsInitialized)
    {
        return 0;
    }
    
    if (!_playing) {
        AudioOutputUnitStop(_audioUnit);
        AudioSessionSetActive(false);
    }

    OSStatus err = noErr;
    // Stop device
    int32_t captureDeviceIsAlive = AtomicGet32(&_captureDeviceIsAlive);
    // We signal a stop for a shared device even when rendering has
    // not yet ended. This is to ensure the IOProc will return early as
    // intended (by checking |_recording|) before accessing
    // resources we free below (e.g. the capture converter).
    //
    // In the case of a shared devcie, the IOProc will verify
    // rendering has ended before stopping itself.
    if (_recording && captureDeviceIsAlive == 1)
    {
        _recording = false;
        _doStop = true; // Signal to io proc to stop audio device
        _critSect.Leave(); // Cannot be under lock, risk of deadlock
        if (kEventTimeout == _stopEvent.Wait(2000))
        {
            CriticalSectionScoped critScoped(_critSect);
            WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                         " Timed out stopping the shared IOProc. "
                         "We may have failed to detect a device removal.");

            // We assume rendering on a shared device has stopped as well if
            // the IOProc times out.
            /*WEBRTC_CA_LOG_WARN(AudioDeviceStop(_outputDeviceID,
                                               _deviceIOProcID));
            WEBRTC_CA_LOG_WARN(AudioDeviceDestroyIOProcID(_outputDeviceID,
                                                          _deviceIOProcID));
             */
        }
        _critSect.Enter();
        _doStop = false;
        WEBRTC_TRACE(kTraceDebug, kTraceAudioDevice, _id,
                     " Recording stopped (shared)");
    }

    // Setting this signal will allow the worker thread to be stopped.
    AtomicSet32(&_captureDeviceIsAlive, 0);
    _critSect.Leave();
    if (_captureWorkerThread != NULL)
    {
        if (!_captureWorkerThread->Stop())
        {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         " Timed out waiting for the render worker thread to "
                             "stop.");
        }
    }
    _critSect.Enter();
    
    WEBRTC_CA_LOG_WARN(AudioConverterDispose(_captureConverter));
    
    _recIsInitialized = false;
    _recording = false;

    return 0;
}

bool AudioDeviceIPhone::RecordingIsInitialized() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return (_recIsInitialized);
}

bool AudioDeviceIPhone::Recording() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return (_recording);
}

bool AudioDeviceIPhone::PlayoutIsInitialized() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return (_playIsInitialized);
}

WebRtc_Word32 AudioDeviceIPhone::StartPlayout()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    
    CriticalSectionScoped lock(_critSect);

    if (!_playIsInitialized)
    {
        return -1;
    }

    if (_playing)
    {
        return 0;
    }

    OSStatus err = noErr;

    unsigned int threadID(0);
    if (_renderWorkerThread != NULL)
    {
        _renderWorkerThread->Start(threadID);
    }
    _renderWorkerThreadId = threadID;

    if (!_recording) {
        AudioSessionSetActive(true);
        OSStatus err = noErr;
        err = AudioOutputUnitStart(_audioUnit);
    }
    
    WEBRTC_CA_RETURN_ON_ERR(AudioConverterNew(&_outStreamFormat, &_outDesiredFormat,
                                              &_renderConverter));

    
    _playing = true;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::StopPlayout()
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    CriticalSectionScoped lock(_critSect);

    if (!_playIsInitialized)
    {
        return 0;
    }
    
    if (!_recording) {
        AudioOutputUnitStop(_audioUnit);
        AudioSessionSetActive(false);
    }

    OSStatus err = noErr;
    int32_t renderDeviceIsAlive = AtomicGet32(&_renderDeviceIsAlive);
    if (_playing && renderDeviceIsAlive == 1)
    {
        // We signal a stop for a shared device even when capturing has not
        // yet ended. This is to ensure the IOProc will return early as
        // intended (by checking |_playing|) before accessing resources we
        // free below (e.g. the render converter).
        //
        // In the case of a shared device, the IOProc will verify capturing
        // has ended before stopping itself.
        _playing = false;
        _doStop = true; // Signal to io proc to stop audio device
        _critSect.Leave(); // Cannot be under lock, risk of deadlock
        if (kEventTimeout == _stopEvent.Wait(2000))
        {
            CriticalSectionScoped critScoped(_critSect);
            WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                         " Timed out stopping the render IOProc. "
                         "We may have failed to detect a device removal.");
        }
        _critSect.Enter();
        _doStop = false;
        WEBRTC_TRACE(kTraceDebug, kTraceAudioDevice, _id,
                     "Playout stopped");
    }

    // Setting this signal will allow the worker thread to be stopped.
    AtomicSet32(&_renderDeviceIsAlive, 0);
    _critSect.Leave();
    if (_renderWorkerThread != NULL)
    {
        if (!_renderWorkerThread->Stop())
        {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         " Timed out waiting for the render worker thread to "
                         "stop.");
        }
    }
    _critSect.Enter();

    WEBRTC_CA_LOG_WARN(AudioConverterDispose(_renderConverter));
    
    _playIsInitialized = false;
    _playing = false;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::PlayoutDelay(WebRtc_UWord16& delayMS) const
{
    int32_t renderDelayUs = AtomicGet32(&_renderDelayUs);
    delayMS = static_cast<WebRtc_UWord16> (1e-3 * (renderDelayUs
        + _renderLatencyUs) + 0.5);
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::RecordingDelay(WebRtc_UWord16& delayMS) const
{
    int32_t captureDelayUs = AtomicGet32(&_captureDelayUs);
    delayMS = static_cast<WebRtc_UWord16> (1e-3 * (captureDelayUs
        + _captureLatencyUs) + 0.5);
    return 0;
}

bool AudioDeviceIPhone::Playing() const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);
    return (_playing);
}

WebRtc_Word32 AudioDeviceIPhone::SetPlayoutBuffer(
    const AudioDeviceModule::BufferType type,
    WebRtc_UWord16 sizeMS)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "AudioDeviceIPhone::SetPlayoutBuffer(type=%u, sizeMS=%u)", type,
                 sizeMS);

    if (type != AudioDeviceModule::kFixedBufferSize)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " Adaptive buffer size not supported on this platform");
        return -1;
    }

    _playBufType = type;
    _playBufDelayFixed = sizeMS;
    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::PlayoutBuffer(
    AudioDeviceModule::BufferType& type,
    WebRtc_UWord16& sizeMS) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    type = _playBufType;
    sizeMS = _playBufDelayFixed;

    return 0;
}

WebRtc_Word32 AudioDeviceIPhone::CPULoad(WebRtc_UWord16& /*load*/) const
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, _id,
                 "%s", __FUNCTION__);

    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, _id,
                 "  API call not supported yet on this platform");

    return -1;
}

bool AudioDeviceIPhone::PlayoutWarning() const
{
    return (_playWarning > 0);
}

bool AudioDeviceIPhone::PlayoutError() const
{
    return (_playError > 0);
}

bool AudioDeviceIPhone::RecordingWarning() const
{
    return (_recWarning > 0);
}

bool AudioDeviceIPhone::RecordingError() const
{
    return (_recError > 0);
}

void AudioDeviceIPhone::ClearPlayoutWarning()
{
    _playWarning = 0;
}

void AudioDeviceIPhone::ClearPlayoutError()
{
    _playError = 0;
}

void AudioDeviceIPhone::ClearRecordingWarning()
{
    _recWarning = 0;
}

void AudioDeviceIPhone::ClearRecordingError()
{
    _recError = 0;
}

bool AudioDeviceIPhone::RunRender(void* ptrThis)
{
    return static_cast<AudioDeviceIPhone*> (ptrThis)->RenderWorkerThread();
}

bool AudioDeviceIPhone::RenderWorkerThread()
{
    ring_buffer_size_t numSamples = ENGINE_PLAY_BUF_SIZE_IN_SAMPLES
    * _outDesiredFormat.mChannelsPerFrame;
    while (PaUtil_GetRingBufferWriteAvailable(_paRenderBuffer)
           - _renderDelayOffsetSamples < numSamples)
    {
        mach_timespec_t timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = TIMER_PERIOD_MS;
        
        kern_return_t kernErr = semaphore_timedwait(_renderSemaphore, timeout);
        if (kernErr == KERN_OPERATION_TIMED_OUT)
        {
            int32_t signal = AtomicGet32(&_renderDeviceIsAlive);
            if (signal == 0)
            {
                // The render device is no longer alive; stop the worker thread.
                return false;
            }
        } else if (kernErr != KERN_SUCCESS)
        {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         " semaphore_timedwait() error: %d", kernErr);
        }
    }
    
    WebRtc_Word8 playBuffer[4 * ENGINE_PLAY_BUF_SIZE_IN_SAMPLES];
    
    if (!_ptrAudioBuffer)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  capture AudioBuffer is invalid");
        return false;
    }
    
    // Ask for new PCM data to be played out using the AudioDeviceBuffer.
    WebRtc_UWord32 nSamples =
    _ptrAudioBuffer->RequestPlayoutData(ENGINE_PLAY_BUF_SIZE_IN_SAMPLES);
    
    nSamples = _ptrAudioBuffer->GetPlayoutData(playBuffer);
    if (nSamples != ENGINE_PLAY_BUF_SIZE_IN_SAMPLES)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     "  invalid number of output samples(%d)", nSamples);
    }
    
    WebRtc_UWord32 nOutSamples = nSamples * _outDesiredFormat.mChannelsPerFrame;
    
    SInt16 *pPlayBuffer = (SInt16 *) &playBuffer;
    
    PaUtil_WriteRingBuffer(_paRenderBuffer, pPlayBuffer, nOutSamples);
    
    kern_return_t kernErr = semaphore_signal_all(_renderSemaphore);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " semaphore_signal_all() error: %d", kernErr);
        return false;
    }
    
    return true;
}

bool AudioDeviceIPhone::RunCapture(void* ptrThis)
{
    return static_cast<AudioDeviceIPhone*> (ptrThis)->CaptureWorkerThread();
}

bool AudioDeviceIPhone::CaptureWorkerThread()
{
    UInt32 noRecSamples = ENGINE_REC_BUF_SIZE_IN_SAMPLES
    * _inDesiredFormat.mChannelsPerFrame;
    SInt16 recordBuffer[noRecSamples];
    UInt32 size = ENGINE_REC_BUF_SIZE_IN_SAMPLES;
  
    AudioBufferList engineBuffer;
    engineBuffer.mNumberBuffers = 1; // Interleaved channels.
    engineBuffer.mBuffers->mNumberChannels = _inDesiredFormat.mChannelsPerFrame;
    engineBuffer.mBuffers->mDataByteSize = _inDesiredFormat.mBytesPerPacket
    * noRecSamples;
    engineBuffer.mBuffers->mData = recordBuffer;
    OSStatus err = AudioConverterFillComplexBuffer(_captureConverter, inConverterProc,
                                          this, &size, &engineBuffer, NULL);
    if (err != noErr)
    {
        if (err == 1)
        {
            // This is our own error.
            return false;
        } else
        {
            logCAMsg(kTraceError, kTraceAudioDevice, _id,
                     "Capture Error in AudioConverterFillComplexBuffer()",
                     (const char *) &err);
            return false;
        }
    }

    // TODO(xians): what if the returned size is incorrect?
    if (size == ENGINE_REC_BUF_SIZE_IN_SAMPLES)
    {
        WebRtc_Word32 msecOnPlaySide;
        WebRtc_Word32 msecOnRecordSide;
        
        int32_t captureDelayUs = AtomicGet32(&_captureDelayUs);
        int32_t renderDelayUs = AtomicGet32(&_renderDelayUs);
        
        msecOnPlaySide = static_cast<WebRtc_Word32> (1e-3 * (renderDelayUs
                                                             + _renderLatencyUs) + 0.5);
        msecOnRecordSide = static_cast<WebRtc_Word32> (1e-3 * (captureDelayUs
                                                               + _captureLatencyUs) + 0.5);
        
        if (!_ptrAudioBuffer)
        {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "  capture AudioBuffer is invalid");
            return false;
        }
        
        // store the recorded buffer (no action will be taken if the
        // #recorded samples is not a full buffer)
        _ptrAudioBuffer->SetRecordedBuffer((WebRtc_Word8*) recordBuffer,
                                           (WebRtc_UWord32) size);
        
        _ptrAudioBuffer->SetVQEData(msecOnPlaySide, msecOnRecordSide, 0);
        
        // deliver recorded samples at specified sample rate, mic level etc.
        // to the observer using callback
        _ptrAudioBuffer->DeliverRecordedData();
    }
    
    return true;
}
    
OSStatus AudioDeviceIPhone::inConverterProc(
                                         AudioConverterRef,
                                         UInt32 *numberDataPackets,
                                         AudioBufferList *data,
                                         AudioStreamPacketDescription ** /*dataPacketDescription*/,
                                         void *userData)
{
    AudioDeviceIPhone *ptrThis = static_cast<AudioDeviceIPhone*> (userData);
    assert(ptrThis != NULL);
    
    return ptrThis->implInConverterProc(numberDataPackets, data);
}

OSStatus AudioDeviceIPhone::implInConverterProc(UInt32 *numberDataPackets,
                                             AudioBufferList *data)
{
    assert(data->mNumberBuffers == 1);
    ring_buffer_size_t numSamples = *numberDataPackets
    * _inStreamFormat.mChannelsPerFrame;
    
    while (PaUtil_GetRingBufferReadAvailable(_paCaptureBuffer) < numSamples)
    {
        mach_timespec_t timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = TIMER_PERIOD_MS;
        
        kern_return_t kernErr = semaphore_timedwait(_captureSemaphore, timeout);
        if (kernErr == KERN_OPERATION_TIMED_OUT)
        {
            int32_t signal = AtomicGet32(&_captureDeviceIsAlive);
            if (signal == 0)
            {
                // The capture device is no longer alive; stop the worker thread.
                *numberDataPackets = 0;
                return 1;
            }
        } else if (kernErr != KERN_SUCCESS)
        {
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         " semaphore_wait() error: %d", kernErr);
        }
    }
    
    // Pass the read pointer directly to the converter to avoid a memcpy.
    void* dummyPtr;
    ring_buffer_size_t dummySize;
    PaUtil_GetRingBufferReadRegions(_paCaptureBuffer, numSamples,
                                    &data->mBuffers->mData, &numSamples,
                                    &dummyPtr, &dummySize);
    //Ignore second region, will be read next time
    PaUtil_AdvanceRingBufferReadIndex(_paCaptureBuffer, numSamples);
    
    data->mBuffers->mNumberChannels = _inStreamFormat.mChannelsPerFrame;
    *numberDataPackets = numSamples / _inStreamFormat.mChannelsPerFrame;
    data->mBuffers->mDataByteSize = *numberDataPackets * _inStreamFormat.mBytesPerPacket;
    
    return 0;
}

OSStatus AudioDeviceIPhone::outConverterProc(AudioConverterRef,
                                          UInt32 *numberDataPackets,
                                          AudioBufferList *data,
                                          AudioStreamPacketDescription **,
                                          void *userData)
{
    AudioDeviceIPhone *ptrThis = (AudioDeviceIPhone *) userData;
    assert(ptrThis != NULL);
    
    return ptrThis->implOutConverterProc(numberDataPackets, data);
}

OSStatus AudioDeviceIPhone::implOutConverterProc(UInt32 *numberDataPackets,
                                                 AudioBufferList *data)
{
    assert(data->mNumberBuffers == 1);
    AudioBuffer *buffer = data->mBuffers;
//    ring_buffer_size_t numSamples = *numberDataPackets * _outDesiredFormat.mChannelsPerFrame;
    
    buffer->mNumberChannels = _outDesiredFormat.mChannelsPerFrame;
    // Always give the converter as much as it wants, zero padding as required.
    buffer->mDataByteSize = *numberDataPackets * _outDesiredFormat.mBytesPerPacket;
    buffer->mData = _renderConvertData;
    memset(_renderConvertData, 0, buffer->mDataByteSize);
    
/*    ring_buffer_size_t numRead = PaUtil_ReadRingBuffer(_paRenderBuffer, _renderConvertData, numSamples);  
    if (numRead < *numberDataPackets) 
    {
        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice,
                     _id, "Missing data while playing");
    }*/
    
    kern_return_t kernErr = semaphore_signal_all(_renderSemaphore);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " semaphore_signal_all() error: %d", kernErr);
        return 1;
    }
    
    return 0;
}

OSStatus AudioDeviceIPhone::inDeviceIOProc(void                        *inRefCon,
                                           AudioUnitRenderActionFlags  *ioActionFlags,
                                           const AudioTimeStamp        *inTimeStamp,
                                           UInt32                      inBusNumber,
                                           UInt32                      inNumberFrames,
                                           AudioBufferList             *ioData)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, -1,
                 "%s samples: %d", __FUNCTION__, inNumberFrames);
    
    AudioDeviceIPhone *ptrThis = (AudioDeviceIPhone *)inRefCon;
    assert(ptrThis != NULL);
    
    AudioBufferList buffers;
    buffers.mNumberBuffers = 1;
    buffers.mBuffers[0].mData = ptrThis->_captureData;
    buffers.mBuffers[0].mNumberChannels = ptrThis->_inStreamFormat.mChannelsPerFrame;
    buffers.mBuffers[0].mDataByteSize = inNumberFrames * ptrThis->_inStreamFormat.mBytesPerFrame *
    ptrThis->_inStreamFormat.mChannelsPerFrame;
    
    ioData = &buffers;
    OSStatus err = noErr;
    err = AudioUnitRender(ptrThis->_audioUnit,
                             ioActionFlags, 
                             inTimeStamp, 
                             inBusNumber, 
                             inNumberFrames, 
                             ioData);
    
    err = ptrThis->implInDeviceIOProc(ioData, inTimeStamp);
    return err;
}
    
OSStatus AudioDeviceIPhone::implInDeviceIOProc(const AudioBufferList *inputData,
                                               const AudioTimeStamp *inputTime)
{        
    UInt64 inputTimeNs = AudioConvertHostTimeToNanos(inputTime->mHostTime);
    UInt64 nowNs = AudioConvertHostTimeToNanos(mach_absolute_time());
    
    OSStatus err = noErr;
    
    // Check if we should close down audio device
    // Double-checked locking optimization to remove locking overhead
    if (_doStopRec)
    {
        _critSect.Enter();
        if (_doStopRec)
        {
            WEBRTC_TRACE(kTraceDebug, kTraceAudioDevice,
                             _id, " Recording device stopped");
            
            _doStopRec = false;
            _stopEventRec.Set();
            _critSect.Leave();
            return 0;
        }
        _critSect.Leave();
    }
    
    if (!_recording)
    {
        // Allow above checks to avoid a timeout on stopping capture.
        return 0;
    }

    ring_buffer_size_t bufSizeSamples =
    PaUtil_GetRingBufferReadAvailable(_paCaptureBuffer);

    int32_t captureDelayUs = static_cast<int32_t> (1e-3 * (nowNs - inputTimeNs)
                                                   + 0.5);
    captureDelayUs
    += static_cast<int32_t> ((1.0e6 * bufSizeSamples)
                             / _inStreamFormat.mChannelsPerFrame / _inStreamFormat.mSampleRate
                             + 0.5);
    
    AtomicSet32(&_captureDelayUs, captureDelayUs);

    assert(inputData->mNumberBuffers == 1);
    
    ring_buffer_size_t numSamples = inputData->mBuffers->mDataByteSize
    * _inStreamFormat.mChannelsPerFrame / _inStreamFormat.mBytesPerPacket;
    if (numSamples > bufSizeSamples) 
    {
        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice,
                     _id, " Recording data while buffer still full");
    }
    PaUtil_WriteRingBuffer(_paCaptureBuffer, inputData->mBuffers->mData,
                           numSamples);
    
    kern_return_t kernErr = semaphore_signal_all(_captureSemaphore);
    if (kernErr != KERN_SUCCESS)
    {
        WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                     " semaphore_signal_all() error: %d", kernErr);
    }
    
    return err;
}

OSStatus AudioDeviceIPhone::outDeviceIOProc(void                        *inRefCon,
                                            AudioUnitRenderActionFlags  *ioActionFlags,
                                            const AudioTimeStamp        *inTimeStamp,
                                            UInt32                      inBusNumber,
                                            UInt32                      inNumberFrames,
                                            AudioBufferList             *ioData)
{
    WEBRTC_TRACE(kTraceModuleCall, kTraceAudioDevice, -1,
                 "%s", __FUNCTION__);
    
    AudioDeviceIPhone *ptrThis = (AudioDeviceIPhone *)inRefCon;
    assert(ptrThis != NULL);

    return ptrThis->implOutDeviceIOProc(ioData, inTimeStamp);
}
    
OSStatus AudioDeviceIPhone::implOutDeviceIOProc(AudioBufferList *outputData, const AudioTimeStamp *outputTime)
{
    OSStatus err = noErr;
    UInt64 outputTimeNs = AudioConvertHostTimeToNanos(outputTime->mHostTime);
    UInt64 nowNs = AudioConvertHostTimeToNanos(mach_absolute_time());

    
    // Check if we should close down audio device
    // Double-checked locking optimization to remove locking overhead
    if (_doStop)
    {
        _critSect.Enter();
        if (_doStop)
        {
            _doStop = false;
            _stopEvent.Set();
            _critSect.Leave();
            return 0;
        }
        _critSect.Leave();
    }
    
    if (!_playing)
    {
        // This can be the case when a shared device is capturing but not
        // rendering. We allow the checks above before returning to avoid a
        // timeout when capturing is stopped.
        return 0;
    }
       
    assert(_outStreamFormat.mBytesPerFrame != 0);
    UInt32 size = outputData->mBuffers->mDataByteSize
    / _outStreamFormat.mBytesPerFrame;

    // TODO(xians): signal an error somehow?
    err = AudioConverterFillComplexBuffer(_renderConverter, outConverterProc,
                                          this, &size, outputData, NULL);
    if (err != noErr)
    {
        if (err == 1)
        {
            // This is our own error.
            WEBRTC_TRACE(kTraceError, kTraceAudioDevice, _id,
                         "Render Error in AudioConverterFillComplexBuffer()");
            return 1;
        } else
        {
            logCAMsg(kTraceError, kTraceAudioDevice, _id,
                     "Render Error in AudioConverterFillComplexBuffer()",
                     (const char *) &err);
            return 1;
        }
    }
    
    ring_buffer_size_t bufSizeSamples =
    PaUtil_GetRingBufferReadAvailable(_paRenderBuffer);

    int32_t renderDelayUs = static_cast<int32_t> (1e-3 * (outputTimeNs - nowNs)
                                                  + 0.5);
    renderDelayUs += static_cast<int32_t> ((1.0e6 * bufSizeSamples)
                                           / _outDesiredFormat.mChannelsPerFrame / _outDesiredFormat.mSampleRate
                                           + 0.5);
    
    AtomicSet32(&_renderDelayUs, renderDelayUs);

    return 0;
}
    
} //  namespace webrtc
