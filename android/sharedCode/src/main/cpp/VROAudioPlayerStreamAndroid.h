//
//  VROAudioPlayerStreamAndroid.h
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

#ifndef VROAudioPlayerStreamAndroid_h
#define VROAudioPlayerStreamAndroid_h

#include "VROAudioPlayer.h"
#include <jni.h>

/*
 Streaming PCM audio player for Android backed by StreamingAudioPlayer.java
 (AudioTrack in MODE_STREAM). Usage mirrors VROAudioPlayerStreamiOS:

   auto player = driver->newStreamingAudioPlayer();
   player->beginStreaming(32000, 2);
   player->play();
   player->pushSamples(floatBuf, count);  // from any thread
*/
class VROAudioPlayerStreamAndroid : public VROAudioPlayer {

public:

    VROAudioPlayerStreamAndroid();
    virtual ~VROAudioPlayerStreamAndroid();

    void setup() override {}
    void setLoop(bool loop) override {}
    void play() override;
    void pause() override;
    void setVolume(float volume) override;
    void setMuted(bool muted) override;
    void seekToTime(float seconds) override {}

    void beginStreaming(int sampleRate, int channels) override;
    size_t pushSamples(const float *data, size_t count) override;
    bool isStreaming() const override { return _streaming; }

private:

    jobject _jPlayer;
    bool _streaming = false;
};

#endif /* VROAudioPlayerStreamAndroid_h */
