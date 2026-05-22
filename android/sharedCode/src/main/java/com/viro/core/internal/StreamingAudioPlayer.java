//
//  StreamingAudioPlayer.java
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

package com.viro.core.internal;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;

/**
 * Streaming PCM audio player for Android using AudioTrack in MODE_STREAM.
 * Controlled via JNI by VROAudioPlayerStreamAndroid.
 */
public class StreamingAudioPlayer {

    private static final String TAG = "ViroStream";

    private AudioTrack mAudioTrack;
    private float mVolume = 1.0f;
    private boolean mMuted = false;
    private boolean mStreaming = false;

    public StreamingAudioPlayer() {}

    public void beginStreaming(int sampleRate, int channels) {
        teardown();

        int channelMask = (channels == 2)
                ? AudioFormat.CHANNEL_OUT_STEREO
                : AudioFormat.CHANNEL_OUT_MONO;

        int minBuf = AudioTrack.getMinBufferSize(sampleRate, channelMask,
                AudioFormat.ENCODING_PCM_FLOAT);
        // 4x min gives ~185 ms headroom at 44.1 kHz stereo (matching iOS ring buffer)
        int bufSize = Math.max(minBuf * 4, sampleRate * channels * 4 / 5);

        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build();

        AudioFormat fmt = new AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                .setSampleRate(sampleRate)
                .setChannelMask(channelMask)
                .build();

        mAudioTrack = new AudioTrack(attrs, fmt, bufSize,
                AudioTrack.MODE_STREAM, AudioManager.AUDIO_SESSION_ID_GENERATE);

        mAudioTrack.setVolume(mMuted ? 0f : mVolume);
        mStreaming = true;
        Log.i(TAG, "StreamingAudioPlayer: beginStreaming " + sampleRate + "Hz ch=" + channels);
    }

    public boolean isStreaming() {
        return mStreaming;
    }

    public void play() {
        if (mAudioTrack != null) {
            mAudioTrack.play();
        }
    }

    public void pause() {
        if (mAudioTrack != null) {
            mAudioTrack.pause();
        }
    }

    public void setVolume(float volume) {
        mVolume = volume;
        if (mAudioTrack != null && !mMuted) {
            mAudioTrack.setVolume(volume);
        }
    }

    public void setMuted(boolean muted) {
        mMuted = muted;
        if (mAudioTrack != null) {
            mAudioTrack.setVolume(muted ? 0f : mVolume);
        }
    }

    // Returns samples written (may be less than data.length if buffer is full).
    public int write(float[] data) {
        if (mAudioTrack == null || !mStreaming) return 0;
        return mAudioTrack.write(data, 0, data.length, AudioTrack.WRITE_NON_BLOCKING);
    }

    public void destroy() {
        teardown();
    }

    private void teardown() {
        if (mAudioTrack != null) {
            try {
                mAudioTrack.pause();
                mAudioTrack.flush();
                mAudioTrack.release();
            } catch (Exception e) {
                Log.w(TAG, "StreamingAudioPlayer teardown error", e);
            }
            mAudioTrack = null;
        }
        mStreaming = false;
    }
}
