//
//  Copyright (c) 2017-present, ViroMedia, Inc.
//  All rights reserved.
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
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

package com.viro.core.internal;

import android.content.Context;
import android.net.Uri;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.DefaultLoadControl;
import com.google.android.exoplayer2.ExoPlayer;
import com.google.android.exoplayer2.MediaItem;
import com.google.android.exoplayer2.PlaybackException;
import com.google.android.exoplayer2.Player;
import com.google.android.exoplayer2.SimpleExoPlayer;
import com.google.android.exoplayer2.extractor.DefaultExtractorsFactory;
import com.google.android.exoplayer2.extractor.ExtractorsFactory;
import com.google.android.exoplayer2.source.MediaSource;
import com.google.android.exoplayer2.source.ProgressiveMediaSource;
import com.google.android.exoplayer2.source.dash.DashMediaSource;
import com.google.android.exoplayer2.source.hls.HlsMediaSource;
import com.google.android.exoplayer2.source.smoothstreaming.SsMediaSource;
import com.google.android.exoplayer2.trackselection.AdaptiveTrackSelection;
import com.google.android.exoplayer2.trackselection.DefaultTrackSelector;
import com.google.android.exoplayer2.upstream.DataSource;
import com.google.android.exoplayer2.upstream.DefaultBandwidthMeter;
import com.google.android.exoplayer2.upstream.DefaultDataSourceFactory;
import com.google.android.exoplayer2.upstream.RawResourceDataSource;
import com.google.android.exoplayer2.util.Util;
import com.google.common.base.Ascii;

/**
 * Wraps the Android ExoPlayer and can be controlled via JNI.
 */
public class AVPlayer {

    private static final String TAG = "Viro";

    /**
     * These states mimic the underlying stats in the Android
     * MediaPlayer. We need to ensure we don't violate any state
     * in the Android MediaPlayer, else it becomes invalid.
     */
    private enum State {
        IDLE,
        PREPARED,
        PAUSED,
        STARTED,
    }

    private final ExoPlayer mExoPlayer;
    private float mVolume;
    private final long mNativeReference;
    private boolean mLoop;
    private State mState;
    private boolean mMute;
    private int mPrevExoPlayerState = -1;
    private boolean mWasBuffering = false;

    public AVPlayer(long nativeReference, Context context) {
        mVolume = 1.0f;
        mNativeReference = nativeReference;
        mLoop = false;
        mState = State.IDLE;
        mMute = false;

        AdaptiveTrackSelection.Factory trackSelectionFactory = new AdaptiveTrackSelection.Factory();
        DefaultTrackSelector trackSelector = new DefaultTrackSelector(context, trackSelectionFactory);
        mExoPlayer = new ExoPlayer.Builder(context).setTrackSelector(trackSelector).setLoadControl(new DefaultLoadControl()).build();

        mExoPlayer.addListener(new Player.Listener() {
            @Override
            public void onPlaybackStateChanged(@Player.State int playbackState) {
                // this function sometimes gets called back w/ the same playbackState.
                if (mPrevExoPlayerState == playbackState) {
                    return;
                }
                mPrevExoPlayerState = playbackState;
                switch (playbackState) {
                    case Player.STATE_BUFFERING:
                        if (!mWasBuffering) {
                            nativeWillBuffer(mNativeReference);
                            mWasBuffering = true;
                        }
                        break;
                    case Player.STATE_READY:
                        if (mWasBuffering) {
                            nativeDidBuffer(mNativeReference);
                            mWasBuffering = false;
                        }
                        break;
                    case Player.STATE_ENDED:
                        if (mLoop) {
                            mExoPlayer.seekToDefaultPosition();
                        }
                        nativeOnFinished(mNativeReference);
                        break;
                }
            }

            @Override
            public void onPlayerError(@NonNull PlaybackException error) {
                Log.w(TAG, "AVPlayer encountered error [" + error + "]", error);
                nativeOnError(mNativeReference, error.getLocalizedMessage());
            }
        });
    }

    public boolean setDataSourceURL(String resourceOrURL, final Context context) {
        try {
            reset();

            Uri uri = Uri.parse(resourceOrURL);
            DataSource.Factory dataSourceFactory;
            ExtractorsFactory extractorsFactory = new DefaultExtractorsFactory();
            if (resourceOrURL.startsWith("res")) {
                // the uri we get is in the form res:/#######, so we want the path
                // which is `/#######`, and the id is the path minus the first char
                int id = Integer.parseInt(uri.getPath().substring(1));
                uri = RawResourceDataSource.buildRawResourceUri(id);
                dataSourceFactory = new DataSource.Factory() {
                    @Override
                    public DataSource createDataSource() {
                        return new RawResourceDataSource(context);
                    }
                };
            } else {
                dataSourceFactory = new DefaultDataSourceFactory(context,
                        Util.getUserAgent(context, "ViroAVPlayer"), new DefaultBandwidthMeter());
            }
            Log.i(TAG, "AVPlayer setting URL to [" + uri + "]");

            MediaSource mediaSource = buildMediaSource(uri, dataSourceFactory, extractorsFactory);

            mExoPlayer.prepare(mediaSource);
            mExoPlayer.seekToDefaultPosition();
            mState = State.PREPARED;

            Log.i(TAG, "AVPlayer prepared for playback");
            nativeOnPrepared(mNativeReference);

            return true;
        } catch (Exception e) {
            Log.w(TAG, "AVPlayer failed to load video at URL [" + resourceOrURL + "]", e);
            reset();

            return false;
        }
    }

    private MediaSource buildMediaSource(Uri uri, DataSource.Factory mediaDataSourceFactory, ExtractorsFactory extractorsFactory) {
        int type = inferContentType(uri);
        switch (type) {
            case C.CONTENT_TYPE_SS:
                return new SsMediaSource.Factory(mediaDataSourceFactory).createMediaSource(MediaItem.fromUri(uri));
            case C.CONTENT_TYPE_DASH:
                return new DashMediaSource.Factory(mediaDataSourceFactory).createMediaSource(MediaItem.fromUri(uri));
            case C.CONTENT_TYPE_HLS:
                return new HlsMediaSource.Factory(mediaDataSourceFactory).createMediaSource(MediaItem.fromUri(uri));
            default:
                // Return an ExtraMediaSource as default.
                return new ProgressiveMediaSource.Factory(mediaDataSourceFactory, extractorsFactory).createMediaSource(MediaItem.fromUri(uri));
        }
    }

    private int inferContentType(Uri uri) {
        String path = uri.getPath();
        return path == null ? C.CONTENT_TYPE_OTHER : inferContentType(path);
    }

    private int inferContentType(String fileName) {
        fileName = Ascii.toLowerCase(fileName);
        if (fileName.endsWith(".mpd")) {
            return C.CONTENT_TYPE_DASH;
        } else if (fileName.endsWith(".m3u8")) {
            return C.CONTENT_TYPE_HLS;
        } else if (fileName.endsWith(".ism") || fileName.endsWith(".isml")
                || fileName.endsWith(".ism/manifest") || fileName.endsWith(".isml/manifest")) {
            return C.CONTENT_TYPE_SS;
        } else {
            return C.CONTENT_TYPE_OTHER;
        }
    }

    public void setVideoSink(Surface videoSink) {
        mExoPlayer.setVideoSurface(videoSink);
    }

    public void reset() {
        mExoPlayer.stop();
        mExoPlayer.seekToDefaultPosition();
        mState = State.IDLE;

        Log.i(TAG, "AVPlayer reset");
    }

    public void destroy() {
        reset();
        mExoPlayer.release();

        Log.i(TAG, "AVPlayer destroyed");
    }

    public void play() {
        if (mState == State.PREPARED || mState == State.PAUSED) {
            mExoPlayer.setPlayWhenReady(true);
            mState = State.STARTED;
        } else {
            Log.w(TAG, "AVPlayer could not play video in " + mState.toString() + " state");
        }
    }

    public void pause() {
        if (mState == State.STARTED) {
            mExoPlayer.setPlayWhenReady(false);
            mState = State.PAUSED;
        } else {
            Log.w(TAG, "AVPlayer could not pause video in " + mState.toString() + " state");
        }
    }

    public boolean isPaused() {
        return mState != State.STARTED;
    }

    public void setLoop(boolean loop) {
        mLoop = loop;
        if (mExoPlayer.getPlaybackState() == ExoPlayer.STATE_ENDED) {
            mExoPlayer.seekToDefaultPosition();
        }
    }

    public void setVolume(float volume) {
        mVolume = volume;
        if (!mMute) {
            mExoPlayer.setVolume(mVolume);
        }
    }

    public void setMuted(boolean muted) {
        mMute = muted;
        if (muted) {
            mExoPlayer.setVolume(0);
        } else {
            mExoPlayer.setVolume(mVolume);
        }
    }

    public void seekToTime(float seconds) {
        if (mState == State.IDLE) {
            Log.w(TAG, "AVPlayer could not seek while in IDLE state");
            return;
        }

        mExoPlayer.seekTo((long) (seconds * 1000));
    }

    public float getCurrentTimeInSeconds() {
        if (mState == State.IDLE) {
            Log.w(TAG, "AVPlayer could not get current time in IDLE state");
            return 0;
        }

        return mExoPlayer.getCurrentPosition() / 1000.0f;
    }

    public float getVideoDurationInSeconds() {
        if (mState == State.IDLE) {
            Log.w(TAG, "AVPlayer could not get video duration in IDLE state");
            return 0;
        } else if (mExoPlayer.getDuration() == C.TIME_UNSET) {
            return 0;
        }

        return mExoPlayer.getDuration() / 1000.0f;
    }

    /**
     * Native Callbacks
     */
    private native void nativeOnPrepared(long ref);

    private native void nativeOnFinished(long ref);

    private native void nativeWillBuffer(long ref);

    private native void nativeDidBuffer(long ref);

    private native void nativeOnError(long ref, String error);
}

