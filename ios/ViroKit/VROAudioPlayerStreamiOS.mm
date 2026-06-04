//
//  VROAudioPlayerStreamiOS.mm
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.

#import <AVFoundation/AVFoundation.h>
#include "VROAudioPlayerStreamiOS.h"
#include "VROLog.h"

VROAudioPlayerStreamiOS::VROAudioPlayerStreamiOS() {
    _ring = std::make_shared<VROPCMRingBuffer>(16384);  // ~185 ms at 44.1 kHz stereo
}

VROAudioPlayerStreamiOS::~VROAudioPlayerStreamiOS() {
    teardown();
}

void VROAudioPlayerStreamiOS::setup() {
    // Nothing to do until beginStreaming() is called.
}

void VROAudioPlayerStreamiOS::beginStreaming(int sampleRate, int channels) {
    teardown();

    _ring->setSampleRate(sampleRate);
    _ring->setChannels(channels);

    // AVAudioSourceNode requires non-interleaved PCM — interleaved:YES causes -10868.
    AVAudioFormat *fmt = [[AVAudioFormat alloc]
                          initWithCommonFormat:AVAudioPCMFormatFloat32
                                   sampleRate:(double)sampleRate
                                     channels:(AVAudioChannelCount)channels
                                  interleaved:NO];
    _format = fmt;

    // Capture a raw pointer to the ring buffer for the real-time block.
    // The block holds a copy of _ring (shared_ptr) to extend its lifetime.
    std::shared_ptr<VROPCMRingBuffer> ring = _ring;
    std::atomic<bool> *playing  = &_playing;
    std::atomic<bool> *muted    = &_muted;
    std::atomic<float> *volume  = &_volume;

    AVAudioSourceNode *node = [[AVAudioSourceNode alloc]
        initWithFormat:fmt
           renderBlock:^OSStatus(BOOL              *isSilence,
                                 const AudioTimeStamp *timestamp,
                                 AVAudioFrameCount     frameCount,
                                 AudioBufferList      *outputData) {
            UInt32 numBuffers = outputData->mNumberBuffers;

            if (!*playing || *muted) {
                for (UInt32 b = 0; b < numBuffers; ++b)
                    memset(outputData->mBuffers[b].mData, 0,
                           outputData->mBuffers[b].mDataByteSize);
                *isSilence = YES;
                return noErr;
            }

            int ch = (int)ring->channels();
            if (ch == 1 || numBuffers == 1) {
                // Mono — ring buffer and non-interleaved buffer are identical.
                float *out = (float *)outputData->mBuffers[0].mData;
                ring->read(out, frameCount);
            } else {
                // Stereo — ring stores interleaved LRLR...; deinterleave into
                // separate L and R buffers. Stack buffer avoids heap allocation
                // on the real-time audio thread (8192 floats = 32 KB, safe).
                float tmp[8192];
                size_t total = (size_t)frameCount * 2;
                if (total > 8192) total = 8192;
                ring->read(tmp, total);

                float *outL = (float *)outputData->mBuffers[0].mData;
                float *outR = (float *)outputData->mBuffers[1].mData;
                AVAudioFrameCount frames = (AVAudioFrameCount)(total / 2);
                for (AVAudioFrameCount i = 0; i < frames; ++i) {
                    outL[i] = tmp[i * 2];
                    outR[i] = tmp[i * 2 + 1];
                }
            }

            float vol = volume->load(std::memory_order_relaxed);
            if (vol != 1.0f) {
                for (UInt32 b = 0; b < numBuffers; ++b) {
                    float *buf = (float *)outputData->mBuffers[b].mData;
                    UInt32 frames = outputData->mBuffers[b].mDataByteSize / sizeof(float);
                    for (UInt32 i = 0; i < frames; ++i) buf[i] *= vol;
                }
            }

            *isSilence = NO;
            return noErr;
        }];
    _sourceNode = node;

    AVAudioEngine *engine = [[AVAudioEngine alloc] init];
    [engine attachNode:node];
    [engine connect:node to:engine.mainMixerNode format:fmt];

    NSError *error = nil;
    [engine prepare];
    if (![engine startAndReturnError:&error]) {
        pinfo("VROAudioPlayerStreamiOS: failed to start AVAudioEngine: %s",
              error.localizedDescription.UTF8String);
        return;
    }
    _engine = engine;
    _streaming = true;

    if (_delegate) {
        _delegate->soundIsReady();
    }
}

void VROAudioPlayerStreamiOS::play() {
    if (!_streaming) {
        pwarn("VROAudioPlayerStreamiOS: play() called before beginStreaming()");
        return;
    }
    _playing.store(true, std::memory_order_relaxed);
}

void VROAudioPlayerStreamiOS::pause() {
    _playing.store(false, std::memory_order_relaxed);
}

void VROAudioPlayerStreamiOS::setVolume(float volume) {
    _volume.store(volume, std::memory_order_relaxed);
}

void VROAudioPlayerStreamiOS::setMuted(bool muted) {
    _muted.store(muted, std::memory_order_relaxed);
}

size_t VROAudioPlayerStreamiOS::pushSamples(const float *data, size_t count) {
    if (!_ring) return 0;
    return _ring->write(data, count);
}

void VROAudioPlayerStreamiOS::teardown() {
    _playing.store(false, std::memory_order_relaxed);
    _streaming = false;
    if (_engine) {
        [_engine stop];
        _engine = nil;
    }
    _sourceNode = nil;
    _format = nil;
}
