//
//  ARSessionRecorder.java
//  ViroCore
//
//  Records an AR session to local storage: video.mp4 (H.264, muxed from the
//  raw YUV camera images ARCore hands native per frame) + session.jsonl
//  (header/imu/pose records). See
//  ViroWorkspace/plans/viro-ar-recording-playback-plan.md §2 for the format.
//
//  Owned by ARScene, which creates one per recording and forwards native's
//  per-frame onRecordingFrame(...) call here (see ARScene.startRecording()).
//  All of MediaCodec/MediaMuxer/SensorManager/file I/O live here rather than
//  in native — there is no NDK media pipeline for this feature; the C++ side
//  (VROARSessionARCore::recordFrameForRecording) only pulls a YUV image +
//  pose + intrinsics out of the ARCore frame and hands them across JNI.
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  MIT License — see LICENSE file.
//
package com.viro.core.internal;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Locale;

public class ARSessionRecorder {

    private static final String TAG = "Viro";

    public interface ErrorListener {
        /** Invoked off the calling thread if the encoder/sidecar fails mid-recording. */
        void onRecordingError(String message);
    }

    private final Context mContext;
    private final ErrorListener mErrorListener;

    private String mOutputDir;
    private FileWriter mSidecar;
    private boolean mWroteHeader;
    private final Object mSidecarLock = new Object();
    private long mFirstTimestampNs = -1;

    // Raw IMU (SensorManager.TYPE_ACCELEROMETER/TYPE_GYROSCOPE — NOT the fused
    // rotation-vector sensor VRTARSceneNavigator uses elsewhere for heading).
    private SensorManager mSensorManager;
    private SensorEventListener mSensorListener;
    private final float[] mLastGyro = new float[3];
    private boolean mHaveGyro = false;
    // TYPE_GRAVITY is Android's own fused gravity estimate — the direct
    // analogue of iOS's CMDeviceMotion.gravity — used only for the `pose`
    // line's ground-truth reference, independent of the raw accel/gyro tap.
    private final float[] mLastGravity = new float[3];

    // Video: MediaCodec in buffer mode (not Surface mode) fed directly with
    // I420 packed from ARCore's YUV_420_888 planes, muxed via MediaMuxer.
    // There's no existing MediaCodec/MediaMuxer usage elsewhere in this
    // codebase to mirror — ViroMediaRecorder's screen-capture feature uses
    // android.media.MediaRecorder with a Surface input instead, which isn't
    // applicable here since we start from raw CPU-side YUV bytes, not a GL
    // render target.
    private MediaCodec mEncoder;
    private MediaMuxer mMuxer;
    private int mVideoTrackIndex = -1;
    private boolean mMuxerStarted;
    private int mWidth, mHeight;
    private byte[] mI420Buffer;

    public ARSessionRecorder(Context context, ErrorListener errorListener) {
        mContext = context;
        mErrorListener = errorListener;
    }

    public synchronized void start(String outputDir) throws IOException {
        File dir = new File(outputDir);
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IOException("Could not create output directory: " + outputDir);
        }
        mOutputDir = outputDir;
        mWroteHeader = false;
        mFirstTimestampNs = -1;
        mHaveGyro = false;

        mSidecar = new FileWriter(new File(dir, "session.jsonl"), false);

        mSensorManager = (SensorManager) mContext.getSystemService(Context.SENSOR_SERVICE);
        if (mSensorManager == null) {
            closeSidecarQuietly();
            throw new IOException("SensorManager unavailable");
        }
        Sensor accel = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        Sensor gyro = mSensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        Sensor gravity = mSensorManager.getDefaultSensor(Sensor.TYPE_GRAVITY);
        if (accel == null || gyro == null) {
            closeSidecarQuietly();
            throw new IOException("Raw accelerometer/gyroscope not available on this device");
        }

        mSensorListener = new SensorEventListener() {
            @Override
            public void onSensorChanged(SensorEvent event) {
                if (event.sensor.getType() == Sensor.TYPE_GYROSCOPE) {
                    System.arraycopy(event.values, 0, mLastGyro, 0, 3);
                    mHaveGyro = true;
                } else if (event.sensor.getType() == Sensor.TYPE_GRAVITY) {
                    System.arraycopy(event.values, 0, mLastGravity, 0, 3);
                } else if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
                    if (!mHaveGyro) {
                        return; // wait for a first gyro sample, same "zip-latest" pairing as iOS
                    }
                    // event.timestamp is nanoseconds since boot (SystemClock.elapsedRealtimeNanos
                    // timebase) — the same clock family ARCore's frame timestamps use, so no
                    // cross-conversion is needed to align this with the `pose`/video timeline.
                    writeImuLine(event.timestamp, event.values[0], event.values[1], event.values[2],
                                 mLastGyro[0], mLastGyro[1], mLastGyro[2]);
                }
            }

            @Override
            public void onAccuracyChanged(Sensor sensor, int accuracy) {}
        };
        Handler handler = new Handler(Looper.getMainLooper());
        mSensorManager.registerListener(mSensorListener, accel, SensorManager.SENSOR_DELAY_GAME, handler);
        mSensorManager.registerListener(mSensorListener, gyro, SensorManager.SENSOR_DELAY_GAME, handler);
        if (gravity != null) {
            mSensorManager.registerListener(mSensorListener, gravity, SensorManager.SENSOR_DELAY_UI, handler);
        }

        // video.mp4 itself (MediaCodec/MediaMuxer) is created lazily on the
        // first onRecordingFrame() call, once we know the real image size.
        mMuxerStarted = false;
        mVideoTrackIndex = -1;
    }

    public synchronized void stop() {
        if (mSensorManager != null && mSensorListener != null) {
            mSensorManager.unregisterListener(mSensorListener);
        }
        mSensorManager = null;
        mSensorListener = null;

        if (mEncoder != null) {
            try {
                drainEncoder(true); // flush any buffered frames before finalizing the mp4
                mEncoder.stop();
            } catch (Exception e) {
                Log.w(TAG, "ARSessionRecorder: encoder.stop() failed", e);
            }
            mEncoder.release();
            mEncoder = null;
        }
        if (mMuxer != null) {
            try {
                if (mMuxerStarted) {
                    mMuxer.stop();
                }
            } catch (Exception e) {
                Log.w(TAG, "ARSessionRecorder: muxer.stop() failed", e);
            }
            mMuxer.release();
            mMuxer = null;
        }
        mMuxerStarted = false;
        closeSidecarQuietly();
    }

    private void closeSidecarQuietly() {
        synchronized (mSidecarLock) {
            if (mSidecar != null) {
                try {
                    mSidecar.close();
                } catch (IOException e) {
                    Log.w(TAG, "ARSessionRecorder: failed to close session.jsonl", e);
                }
                mSidecar = null;
            }
        }
    }

    private void reportError(String message) {
        Log.e(TAG, "ARSessionRecorder: " + message);
        if (mErrorListener != null) {
            mErrorListener.onRecordingError(message);
        }
    }

    /**
     * Called from native (VROARSessionARCore::recordFrameForRecording, via
     * JNI) once per frame. dims = [width, height, yRowStride, uRowStride,
     * uPixelStride, vRowStride, vPixelStride]; pose = [qx,qy,qz,qw, px,py,pz,
     * fx,fy,cx,cy] (11 floats) — orientation as a quaternion rather than a
     * full matrix, same convention as the iOS recorder and the plan's
     * session.jsonl format.
     */
    public synchronized void onRecordingFrame(byte[] y, byte[] u, byte[] v, int[] dims,
                                               long timestampNs, float[] pose) {
        if (mSidecar == null) {
            return; // stop() already ran
        }
        int width = dims[0], height = dims[1];
        int yStride = dims[2], uStride = dims[3], uPixelStride = dims[4];
        int vStride = dims[5], vPixelStride = dims[6];

        if (mFirstTimestampNs < 0) {
            mFirstTimestampNs = timestampNs;
        }

        writeHeaderIfNeeded(width, height, pose[7], pose[8], pose[9], pose[10]);
        writePoseLine(timestampNs, pose);

        try {
            encodeFrame(y, u, v, width, height, yStride, uStride, uPixelStride, vStride, vPixelStride,
                        timestampNs - mFirstTimestampNs);
        } catch (Exception e) {
            reportError("video encode failed: " + e.getMessage());
        }
    }

    private void writeHeaderIfNeeded(int width, int height, float fx, float fy, float cx, float cy) {
        if (mWroteHeader) {
            return;
        }
        mWroteHeader = true;
        // Extrinsics default to identity — a safe fallback per the plan;
        // ARCore does not expose a per-device IMU/camera calibration to do
        // better here (matches the iOS recorder's same default).
        String line = String.format(Locale.US,
            "{\"type\":\"header\",\"intrinsics\":{\"fx\":%.4f,\"fy\":%.4f,\"cx\":%.4f,\"cy\":%.4f,\"width\":%d,\"height\":%d}," +
            "\"extrinsics\":{\"q_imu_cam\":[0,0,0,1],\"p_imu_cam\":[0,0,0],\"time_offset\":0.0}}",
            fx, fy, cx, cy, width, height);
        writeLine(line);
    }

    private void writePoseLine(long timestampNs, float[] pose) {
        String line = String.format(Locale.US,
            "{\"type\":\"pose\",\"t\":%d,\"orientation\":[%.6f,%.6f,%.6f,%.6f]," +
            "\"position\":[%.6f,%.6f,%.6f],\"gravity\":[%.6f,%.6f,%.6f]}",
            timestampNs, pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6],
            mLastGravity[0], mLastGravity[1], mLastGravity[2]);
        writeLine(line);
    }

    private void writeImuLine(long timestampNs, float ax, float ay, float az,
                               float gx, float gy, float gz) {
        String line = String.format(Locale.US,
            "{\"type\":\"imu\",\"t\":%d,\"accel\":[%.6f,%.6f,%.6f],\"gyro\":[%.6f,%.6f,%.6f]}",
            timestampNs, ax, ay, az, gx, gy, gz);
        writeLine(line);
    }

    private void writeLine(String line) {
        synchronized (mSidecarLock) {
            if (mSidecar == null) {
                return;
            }
            try {
                mSidecar.write(line);
                mSidecar.write("\n");
                mSidecar.flush();
            } catch (IOException e) {
                reportError("session.jsonl write failed: " + e.getMessage());
            }
        }
    }

    // Packs ARCore's YUV_420_888 planes (arbitrary row/pixel strides) into a
    // tightly-packed I420 (planar Y, then U, then V) buffer — the layout
    // COLOR_FormatYUV420Flexible/COLOR_FormatYUV420Planar encoders expect.
    // Done with a defensive per-pixel copy rather than assuming a stride
    // layout, since ARCore's actual U/V pixel stride varies by device/driver
    // (often 2, for NV21-style interleaving, but not guaranteed).
    private void packI420(byte[] y, byte[] u, byte[] v, int width, int height,
                          int yStride, int uStride, int uPixelStride, int vStride, int vPixelStride) {
        int frameSize = width * height;
        int chromaWidth = width / 2, chromaHeight = height / 2;
        if (mI420Buffer == null || mI420Buffer.length != frameSize + 2 * chromaWidth * chromaHeight) {
            mI420Buffer = new byte[frameSize + 2 * chromaWidth * chromaHeight];
        }
        int dst = 0;
        for (int row = 0; row < height; row++) {
            System.arraycopy(y, row * yStride, mI420Buffer, dst, width);
            dst += width;
        }
        for (int row = 0; row < chromaHeight; row++) {
            int rowBase = row * uStride;
            for (int col = 0; col < chromaWidth; col++) {
                mI420Buffer[dst++] = u[rowBase + col * uPixelStride];
            }
        }
        for (int row = 0; row < chromaHeight; row++) {
            int rowBase = row * vStride;
            for (int col = 0; col < chromaWidth; col++) {
                mI420Buffer[dst++] = v[rowBase + col * vPixelStride];
            }
        }
    }

    private void encodeFrame(byte[] y, byte[] u, byte[] v, int width, int height,
                             int yStride, int uStride, int uPixelStride, int vStride, int vPixelStride,
                             long presentationTimeNs) throws IOException {
        if (mEncoder == null) {
            initEncoder(width, height);
        }
        packI420(y, u, v, width, height, yStride, uStride, uPixelStride, vStride, vPixelStride);

        int inputIndex = mEncoder.dequeueInputBuffer(10_000);
        if (inputIndex >= 0) {
            ByteBuffer inputBuffer = mEncoder.getInputBuffer(inputIndex);
            inputBuffer.clear();
            inputBuffer.put(mI420Buffer);
            long presentationTimeUs = presentationTimeNs / 1000;
            mEncoder.queueInputBuffer(inputIndex, 0, mI420Buffer.length, presentationTimeUs, 0);
        }
        drainEncoder(false);
    }

    private void initEncoder(int width, int height) throws IOException {
        mWidth = width;
        mHeight = height;
        MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
        format.setInteger(MediaFormat.KEY_BIT_RATE, 11_000_000);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, 30);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);

        mEncoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
        mEncoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        mEncoder.start();

        mMuxer = new MediaMuxer(mOutputDir + "/video.mp4", MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
        mMuxerStarted = false;
        mVideoTrackIndex = -1;
    }

    private void drainEncoder(boolean endOfStream) {
        if (endOfStream) {
            try {
                mEncoder.signalEndOfInputStream();
            } catch (Exception e) {
                Log.w(TAG, "ARSessionRecorder: signalEndOfInputStream failed", e);
            }
        }
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        while (true) {
            int outputIndex = mEncoder.dequeueOutputBuffer(info, 10_000);
            if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                mVideoTrackIndex = mMuxer.addTrack(mEncoder.getOutputFormat());
                mMuxer.start();
                mMuxerStarted = true;
            } else if (outputIndex >= 0) {
                ByteBuffer encodedData = mEncoder.getOutputBuffer(outputIndex);
                if (encodedData != null && info.size > 0 && mMuxerStarted) {
                    encodedData.position(info.offset);
                    encodedData.limit(info.offset + info.size);
                    mMuxer.writeSampleData(mVideoTrackIndex, encodedData, info);
                }
                mEncoder.releaseOutputBuffer(outputIndex, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    break;
                }
            } else {
                break; // INFO_TRY_AGAIN_LATER or INFO_OUTPUT_BUFFERS_CHANGED (deprecated path, ignored)
            }
        }
    }
}
