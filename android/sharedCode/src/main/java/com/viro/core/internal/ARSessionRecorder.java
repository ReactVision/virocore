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
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class ARSessionRecorder {

    private static final String TAG = "Viro";
    // How long stop() waits for the encoder to take the end of the stream and hand back its last frames.
    private static final long END_OF_STREAM_TIMEOUT_NS = 2_000_000_000L;
    // Depth frames waiting to be encoded and written; past this, new ones are dropped rather
    // than letting a slow disk grow the queue (each holds a depth map and its confidence).
    private static final int MAX_PENDING_DEPTH_FRAMES = 12;
    // How long stop() waits for queued depth frames to reach the disk.
    private static final long DEPTH_DRAIN_TIMEOUT_S = 30;

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
    // Only used by the packed fallback in fillInputBuffer().
    private byte[] mI420Buffer;
    private boolean mWarnedNoInputImage = false;

    // Depth: PNG encoding and file writes happen on mDepthExecutor, since
    // onRecordingFrame() runs on the GL thread. mDepthActive is set once from native
    // before the first frame; the rest is touched by onRecordingFrame() and the executor.
    private boolean mDepthActive = false;
    private ExecutorService mDepthExecutor;
    private final AtomicInteger mPendingDepthFrames = new AtomicInteger();
    private int mNextDepthIndex = 0;

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
        mDepthActive = false;
        mNextDepthIndex = 0;

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

    /**
     * Called from native once the recording has started, before the first frame: whether
     * this recording carries depth. The header's `depth` field says so, so it is only
     * true when depth can really be written (ARCore measuring it, and depth/ creatable).
     */
    public synchronized void setDepthActive(boolean active) {
        if (active && mOutputDir != null) {
            File depthDir = new File(mOutputDir, "depth");
            active = depthDir.isDirectory() || depthDir.mkdirs();
            if (!active) {
                Log.w(TAG, "ARSessionRecorder: could not create " + depthDir + ", recording without depth");
            }
        }
        mDepthActive = active;
        if (active && mDepthExecutor == null) {
            mDepthExecutor = Executors.newSingleThreadExecutor();
        }
    }

    public synchronized void stop() {
        if (mSensorManager != null && mSensorListener != null) {
            mSensorManager.unregisterListener(mSensorListener);
        }
        mSensorManager = null;
        mSensorListener = null;

        // Unlike iOS's AVAssetWriter (async finishWriting + completion handler,
        // racing against a concurrent appendPixelBuffer if not carefully
        // sequenced — see VROARSessionRecorderIOS.stop()), MediaCodec.stop()/
        // MediaMuxer.stop() are synchronous, blocking calls, and this whole
        // method is `synchronized` on the same monitor as onRecordingFrame(),
        // so there is no equivalent race here: either onRecordingFrame() is
        // still running and this blocks until it finishes, or it can't start
        // once this method holds the lock. What IS missing without the
        // reportError() calls below is failure *visibility* — a thrown
        // exception here was only Log.w'd, so getRecordingStatus() could
        // still report success on an encoder/muxer that failed to finalize.
        if (mEncoder != null) {
            try {
                drainEncoder(true); // flush any buffered frames before finalizing the mp4
                mEncoder.stop();
            } catch (Exception e) {
                Log.w(TAG, "ARSessionRecorder: encoder.stop() failed", e);
                reportError("encoder.stop() failed: " + e.getMessage());
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
                reportError("muxer.stop() failed: " + e.getMessage());
            }
            mMuxer.release();
            mMuxer = null;
        }
        mMuxerStarted = false;
        drainDepthWrites();
        flushBufferedLinesSorted();
        closeSidecarQuietly();
    }

    // Every depth line must be buffered before the sorted flush, so wait for the queue.
    private void drainDepthWrites() {
        ExecutorService executor = mDepthExecutor;
        mDepthExecutor = null;
        mDepthActive = false;
        if (executor == null) {
            return;
        }
        executor.shutdown();
        try {
            if (!executor.awaitTermination(DEPTH_DRAIN_TIMEOUT_S, TimeUnit.SECONDS)) {
                executor.shutdownNow();
                reportError("depth frames were still being written after "
                        + DEPTH_DRAIN_TIMEOUT_S + " s, the recording's depth is incomplete");
            }
        } catch (InterruptedException e) {
            executor.shutdownNow();
            Thread.currentThread().interrupt();
        }
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
     * full matrix, same convention as the iOS recorder's session.jsonl
     * format. geo = [hasGps(0/1), lat, lon, alt, hAccuracy, heading,
     * headingAccuracy], doubles so lat/lon keep centimetre precision; only
     * attached to this one pose line when hasGps is 1. A negative or
     * non-finite heading accuracy means there is no valid heading, and the
     * heading is written as null.
     * <p>
     * depth, depthConfidence, depthDims = [width, height] and depthTimestampNs are null/0
     * unless this frame carries a new depth image: depth is tightly packed little-endian
     * 16-bit millimetres, depthConfidence tightly packed 8-bit, both of the depth map's
     * size. The depth image can be older than this frame, hence its own timestamp.
     */
    public synchronized void onRecordingFrame(byte[] y, byte[] u, byte[] v, int[] dims,
                                               long timestampNs, float[] pose, double[] geo,
                                               byte[] depth, byte[] depthConfidence, int[] depthDims,
                                               long depthTimestampNs) {
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
        writePoseLine(timestampNs, pose, geo);
        if (mDepthActive && depth != null && depthConfidence != null && depthDims != null) {
            enqueueDepthFrame(depth, depthConfidence, depthDims[0], depthDims[1], depthTimestampNs,
                              width, height, pose[7], pose[8], pose[9], pose[10]);
        }

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
        // Extrinsics default to identity — a safe fallback;
        // ARCore does not expose a per-device IMU/camera calibration to do
        // better here (matches the iOS recorder's same default).
        String line = String.format(Locale.US,
            "{\"type\":\"header\",\"intrinsics\":{\"fx\":%.4f,\"fy\":%.4f,\"cx\":%.4f,\"cy\":%.4f,\"width\":%d,\"height\":%d}," +
            "\"extrinsics\":{\"q_imu_cam\":[0,0,0,1],\"p_imu_cam\":[0,0,0],\"time_offset\":0.0},\"depth\":%b}",
            fx, fy, cx, cy, width, height, mDepthActive);
        // Written immediately, not buffered — the header must be the sidecar's
        // first line, which it always is as long as it's flushed before any
        // buffered imu/pose line (guaranteed: header is only ever written from
        // onRecordingFrame() on the first call, before that call's own
        // writePoseLine(), and before flushBufferedLinesSorted() ever runs).
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

    private void writePoseLine(long timestampNs, float[] pose, double[] geo) {
        StringBuilder sb = new StringBuilder(192);
        sb.append(String.format(Locale.US,
            "{\"type\":\"pose\",\"t\":%d,\"orientation\":[%.6f,%.6f,%.6f,%.6f]," +
            "\"position\":[%.6f,%.6f,%.6f],\"gravity\":[%.6f,%.6f,%.6f]",
            timestampNs, pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6],
            mLastGravity[0], mLastGravity[1], mLastGravity[2]));
        // geo[0] (hasGps) gates the whole field. A position without a valid
        // heading is still written, with the heading fields null.
        if (geo != null && geo.length >= 7 && geo[0] != 0
                && isFinite(geo[1]) && isFinite(geo[2]) && isFinite(geo[3])) {
            String hAcc = isFinite(geo[4]) && geo[4] >= 0 ? String.format(Locale.US, "%.3f", geo[4]) : "null";
            boolean headingOk = isFinite(geo[5]) && geo[5] >= 0 && geo[5] < 360
                    && isFinite(geo[6]) && geo[6] >= 0;
            String heading = headingOk ? String.format(Locale.US, "%.3f", geo[5]) : "null";
            String headingAcc = headingOk ? String.format(Locale.US, "%.3f", geo[6]) : "null";
            sb.append(String.format(Locale.US,
                ",\"gps\":{\"lat\":%.8f,\"lon\":%.8f,\"alt\":%.3f," +
                "\"h_accuracy\":%s,\"heading\":%s,\"heading_accuracy\":%s}",
                geo[1], geo[2], geo[3], hAcc, heading, headingAcc));
        }
        sb.append("}");
        bufferLine(timestampNs, sb.toString());
    }

    // Intrinsics are the camera image's; the depth map is a different size, so each axis is
    // scaled by its own ratio. Encoding and writing happen off this thread; the depth line goes
    // through the same sorted buffer as pose, and only once both PNGs are on disk.
    private void enqueueDepthFrame(final byte[] depth, final byte[] confidence,
                                   final int depthWidth, final int depthHeight,
                                   final long depthTimestampNs, int imageWidth, int imageHeight,
                                   float fx, float fy, float cx, float cy) {
        ExecutorService executor = mDepthExecutor;
        if (executor == null || depthWidth <= 0 || depthHeight <= 0 || imageWidth <= 0 || imageHeight <= 0) {
            return;
        }
        if (mPendingDepthFrames.get() >= MAX_PENDING_DEPTH_FRAMES) {
            Log.w(TAG, "ARSessionRecorder: depth writes are behind, dropping a depth frame");
            return;
        }
        final float sx = (float) depthWidth / imageWidth;
        final float sy = (float) depthHeight / imageHeight;
        final float dfx = fx * sx, dfy = fy * sy, dcx = cx * sx, dcy = cy * sy;
        final String depthName = String.format(Locale.US, "depth/%06d.png", mNextDepthIndex);
        final String confidenceName = String.format(Locale.US, "depth/%06d_c.png", mNextDepthIndex);
        mNextDepthIndex++;
        final String outputDir = mOutputDir;

        mPendingDepthFrames.incrementAndGet();
        executor.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    writeFile(new File(outputDir, depthName),
                              PngGrayEncoder.encodeGray16(depth, depthWidth, depthHeight, true));
                    writeFile(new File(outputDir, confidenceName),
                              PngGrayEncoder.encodeGray8(confidence, depthWidth, depthHeight));
                } catch (Exception e) {
                    Log.w(TAG, "ARSessionRecorder: depth frame " + depthName + " not written", e);
                    new File(outputDir, depthName).delete();
                    new File(outputDir, confidenceName).delete();
                    return;
                } finally {
                    mPendingDepthFrames.decrementAndGet();
                }
                String line = String.format(Locale.US,
                    "{\"type\":\"depth\",\"t\":%d,\"file\":\"%s\",\"confidence\":\"%s\"," +
                    "\"width\":%d,\"height\":%d,\"scale\":1000," +
                    "\"fx\":%.4f,\"fy\":%.4f,\"cx\":%.4f,\"cy\":%.4f,\"source\":\"arcore\"}",
                    depthTimestampNs, depthName, confidenceName, depthWidth, depthHeight,
                    dfx, dfy, dcx, dcy);
                bufferLine(depthTimestampNs, line);
            }
        });
    }

    private static void writeFile(File file, byte[] bytes) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(bytes);
        }
    }

    private static boolean isFinite(double v) {
        return !Double.isNaN(v) && !Double.isInfinite(v);
    }

    private void writeImuLine(long timestampNs, float ax, float ay, float az,
                               float gx, float gy, float gz) {
        String line = String.format(Locale.US,
            "{\"type\":\"imu\",\"t\":%d,\"accel\":[%.6f,%.6f,%.6f],\"gyro\":[%.6f,%.6f,%.6f]}",
            timestampNs, ax, ay, az, gx, gy, gz);
        bufferLine(timestampNs, line);
    }

    // imu/pose lines are buffered here rather than written straight to
    // mSidecar, then sorted by timestamp and flushed at stop() — see
    // flushBufferedLinesSorted(). Without this, the format's documented
    // "monotonic timestamp_ns order" guarantee breaks: imu arrives on the
    // SensorManager listener (main Looper thread) while pose is written from
    // onRecordingFrame() (the native/GL callback thread, doing much more work
    // per call — image packing + MediaCodec encode), so pose lines can land
    // in the file later than their own timestamp would place them relative to
    // imu, even though each stream is individually monotonic. Matches the
    // same fix applied to VROARSessionRecorderIOS.
    private final java.util.List<long[]> mBufferedTimestamps = new java.util.ArrayList<>();
    private final java.util.List<String> mBufferedLines = new java.util.ArrayList<>();

    private void bufferLine(long timestampNs, String line) {
        synchronized (mSidecarLock) {
            if (mSidecar == null) {
                return;
            }
            mBufferedTimestamps.add(new long[]{timestampNs, mBufferedLines.size()});
            mBufferedLines.add(line);
        }
    }

    private void flushBufferedLinesSorted() {
        synchronized (mSidecarLock) {
            if (mSidecar == null) {
                return;
            }
            // Stable sort by timestamp (ties broken by original insertion
            // index, held in element [1]) so same-timestamp lines keep their
            // original relative order rather than being shuffled arbitrarily.
            java.util.Collections.sort(mBufferedTimestamps, (a, b) -> {
                int cmp = Long.compare(a[0], b[0]);
                return cmp != 0 ? cmp : Long.compare(a[1], b[1]);
            });
            try {
                for (long[] entry : mBufferedTimestamps) {
                    mSidecar.write(mBufferedLines.get((int) entry[1]));
                    mSidecar.write("\n");
                }
                mSidecar.flush();
            } catch (IOException e) {
                reportError("session.jsonl write failed: " + e.getMessage());
            }
            mBufferedTimestamps.clear();
            mBufferedLines.clear();
        }
    }

    // Fallback only — see fillInputBuffer(). Tightly-packed planar I420, which is
    // right only for genuinely planar encoders; NV12 ones get scrambled chroma.
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

        int inputIndex = mEncoder.dequeueInputBuffer(10_000);
        if (inputIndex >= 0) {
            fillInputBuffer(inputIndex, y, u, v, width, height,
                            yStride, uStride, uPixelStride, vStride, vPixelStride);
            long presentationTimeUs = presentationTimeNs / 1000;
            // Nominal 4:2:0 size — a size declaration, not a layout claim.
            int frameBytes = width * height * 3 / 2;
            mEncoder.queueInputBuffer(inputIndex, 0, frameBytes, presentationTimeUs, 0);
        }
        drainEncoder(false);
    }

    // COLOR_FormatYUV420Flexible promises 8-bit 4:2:0, not a byte layout — the real
    // one is device-specific (commonly NV12 semi-planar, padded row stride). So go
    // through getInputImage(), which exposes the encoder's actual planes and strides.
    private void fillInputBuffer(int inputIndex, byte[] y, byte[] u, byte[] v,
                                 int width, int height,
                                 int yStride, int uStride, int uPixelStride,
                                 int vStride, int vPixelStride) {
        Image image = null;
        try {
            image = mEncoder.getInputImage(inputIndex);
        } catch (Exception e) {
            // Same as unsupported Image mode: fall through to the packed path.
            Log.w(TAG, "ARSessionRecorder: getInputImage failed, using packed fallback", e);
        }

        if (image != null) {
            Image.Plane[] planes = image.getPlanes();
            copyPlane(y, yStride, 1, planes[0], width, height);
            copyPlane(u, uStride, uPixelStride, planes[1], width / 2, height / 2);
            copyPlane(v, vStride, vPixelStride, planes[2], width / 2, height / 2);
            return;
        }

        if (!mWarnedNoInputImage) {
            mWarnedNoInputImage = true;
            Log.w(TAG, "ARSessionRecorder: encoder has no input Image; falling back to packed "
                    + "I420, chroma may be incorrect on semi-planar encoders");
        }
        packI420(y, u, v, width, height, yStride, uStride, uPixelStride, vStride, vPixelStride);
        ByteBuffer inputBuffer = mEncoder.getInputBuffer(inputIndex);
        inputBuffer.clear();
        inputBuffer.put(mI420Buffer);
    }

    // Both sides carry their own row/pixel stride; neither is assumed packed.
    private static void copyPlane(byte[] src, int srcRowStride, int srcPixelStride,
                                  Image.Plane dst, int w, int h) {
        copyPlane(src, srcRowStride, srcPixelStride,
                  dst.getBuffer(), dst.getRowStride(), dst.getPixelStride(), w, h);
    }

    // Split out so the stride arithmetic is testable off-device (Image.Plane cannot
    // be constructed in a host-side test; a ByteBuffer plus its strides can).
    static void copyPlane(byte[] src, int srcRowStride, int srcPixelStride,
                          ByteBuffer buf, int dstRowStride, int dstPixelStride, int w, int h) {
        if (srcPixelStride == 1 && dstPixelStride == 1) {
            // Both sides packed within the row: bulk-copy row by row.
            for (int row = 0; row < h; row++) {
                buf.position(row * dstRowStride);
                buf.put(src, row * srcRowStride, w);
            }
            return;
        }

        // At least one side interleaves (NV12/NV21): copy pixel by pixel.
        for (int row = 0; row < h; row++) {
            int srcBase = row * srcRowStride;
            int dstBase = row * dstRowStride;
            for (int col = 0; col < w; col++) {
                buf.position(dstBase + col * dstPixelStride);
                buf.put(src[srcBase + col * srcPixelStride]);
            }
        }
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
        // Container-level only: players show it upright while the frames stay
        // sensor-native, so they still match session.jsonl's intrinsics. Consumers
        // decoding for tracking must pass ffmpeg -noautorotate.
        mMuxer.setOrientationHint(getSensorOrientationDegrees());
        mMuxerStarted = false;
        mVideoTrackIndex = -1;
    }

    // Degrees to rotate the sensor image upright on a device held portrait. Read from
    // the rear camera rather than hardcoded; 90 is the fallback and the common value.
    private int getSensorOrientationDegrees() {
        try {
            CameraManager cm = (CameraManager) mContext.getSystemService(Context.CAMERA_SERVICE);
            if (cm != null) {
                for (String id : cm.getCameraIdList()) {
                    CameraCharacteristics cc = cm.getCameraCharacteristics(id);
                    Integer facing = cc.get(CameraCharacteristics.LENS_FACING);
                    if (facing != null && facing == CameraCharacteristics.LENS_FACING_BACK) {
                        Integer orientation = cc.get(CameraCharacteristics.SENSOR_ORIENTATION);
                        if (orientation != null) {
                            return orientation;
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "ARSessionRecorder: could not read SENSOR_ORIENTATION, assuming 90", e);
        }
        return 90;
    }

    private void drainEncoder(boolean endOfStream) {
        boolean waitForEnd = endOfStream && queueEndOfStream();
        long deadline = System.nanoTime() + END_OF_STREAM_TIMEOUT_NS;
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
            } else if (waitForEnd && System.nanoTime() < deadline) {
                continue; // the encoder is still flushing the frames before the end
            } else {
                if (waitForEnd) {
                    Log.w(TAG, "ARSessionRecorder: encoder did not finish; the last frames may be missing");
                }
                break; // INFO_TRY_AGAIN_LATER or INFO_OUTPUT_BUFFERS_CHANGED (deprecated path, ignored)
            }
        }
    }

    /**
     * Frames go in as byte buffers, so the stream ends with an empty buffer
     * flagged end-of-stream: signalEndOfInputStream() is only for an input
     * Surface, and throws here. False when no input buffer came free in time.
     */
    private boolean queueEndOfStream() {
        long deadline = System.nanoTime() + END_OF_STREAM_TIMEOUT_NS;
        while (System.nanoTime() < deadline) {
            int inputIndex = mEncoder.dequeueInputBuffer(10_000);
            if (inputIndex >= 0) {
                mEncoder.queueInputBuffer(inputIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                return true;
            }
            drainEncoder(false);
        }
        Log.w(TAG, "ARSessionRecorder: no encoder input buffer to end the stream; the last frames may be missing");
        return false;
    }
}
