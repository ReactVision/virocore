//
//  CameraTexture.java
//  ViroRenderer
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
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

/*
 * Java wrapper for VROCameraTextureAndroid.
 *
 * Architecture mirrors VideoTexture:
 *   1. Java constructor calls nativeCreate() → creates VROCameraTextureAndroid, returns native ref.
 *   2. nativeInit() dispatches initCamera() to the GL thread via VROPlatformDispatchAsyncRenderer.
 *   3. initCamera() generates the OES texture, creates the VROTextureSubstrate, registers
 *      as a VROFrameListener, then calls back to Java via onGLReady(glTextureId).
 *   4. onGLReady() creates SurfaceTexture(glTextureId) and opens the Camera2 session.
 *   5. VROCameraTextureAndroid::onFrameWillRender() calls updateTexImage() each GL frame.
 *
 * Capture support:
 *   - Photo:   capturePhoto(outputPath, callback) fires a JPEG still via ImageReader.
 *   - Video:   startRecording(outputPath, callback) / stopRecording(callback) use
 *              MediaRecorder with a Surface target added to the Camera2 session.
 *              The session is recreated when recording starts/stops.
 */
package com.viro.core;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaRecorder;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class CameraTexture extends Texture {

    private static final String TAG = "CameraTexture";

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    public enum Position { FRONT, BACK }

    /** Called once when the first camera frame is available. */
    public interface ReadyListener {
        void onCameraReady();
    }

    /** Async callback for capturePhoto / startRecording / stopRecording. */
    public interface CaptureCallback {
        void onSuccess(String outputPath);
        void onError(String error);
    }

    // -----------------------------------------------------------------------
    // State — preview
    // -----------------------------------------------------------------------

    private final boolean mFrontFacing;
    private SurfaceTexture mSurfaceTexture;

    private CameraManager mCameraManager;
    private CameraDevice  mCameraDevice;
    private CameraCaptureSession mCaptureSession;

    private HandlerThread mBackgroundThread;
    private Handler       mBackgroundHandler;

    private volatile boolean mPaused    = true;
    private volatile boolean mDisposed  = false;
    private boolean mFirstFrameFired    = false;

    private ReadyListener mReadyListener;

    // -----------------------------------------------------------------------
    // State — photo capture (ImageReader)
    // -----------------------------------------------------------------------

    private ImageReader    mImageReader;
    private String         mPendingPhotoPath;
    private CaptureCallback mPendingPhotoCallback;

    // -----------------------------------------------------------------------
    // State — video recording (MediaRecorder)
    // -----------------------------------------------------------------------

    private MediaRecorder  mMediaRecorder;
    private boolean        mIsRecording           = false;
    private boolean        mPendingStartRecording = false;  // start after session reconfigured
    private String         mPendingRecordingPath;
    private CaptureCallback mRecordingStartCallback;

    // -----------------------------------------------------------------------
    // Constructor
    // -----------------------------------------------------------------------

    private Context mAppContext;

    public CameraTexture(ViroContext viroContext, Position position, Context appContext) {
        mFrontFacing = (position == Position.FRONT);
        mAppContext = appContext.getApplicationContext();
        mNativeRef = nativeCreate(mFrontFacing);
        nativeInit(mNativeRef, viroContext.mNativeRef);
    }

    // -----------------------------------------------------------------------
    // ReadyListener
    // -----------------------------------------------------------------------

    public void setReadyListener(ReadyListener listener) {
        mReadyListener = listener;
    }

    // -----------------------------------------------------------------------
    // Play / Pause / Dispose
    // -----------------------------------------------------------------------

    public void play() {
        mPaused = false;
        if (mSurfaceTexture != null) startCamera();
        nativePlay(mNativeRef);
    }

    public void pause() {
        mPaused = true;
        stopCamera();
        nativePause(mNativeRef);
    }

    public boolean isPaused() { return mPaused; }

    @Override
    public void dispose() {
        if (mDisposed) return;
        mDisposed = true;
        if (mIsRecording) safeStopMediaRecorder();
        stopCamera();
        stopBackgroundThread();
        if (mImageReader != null) { mImageReader.close(); mImageReader = null; }
        if (mSurfaceTexture != null) { mSurfaceTexture.release(); mSurfaceTexture = null; }
        nativeDispose(mNativeRef);
        mNativeRef = 0;
    }

    // -----------------------------------------------------------------------
    // GL-thread callbacks from VROCameraTextureAndroid
    // -----------------------------------------------------------------------

    /** Called by C++ after the OES texture is created on the GL thread. */
    public void onGLReady(int glTextureId) {
        mSurfaceTexture = new SurfaceTexture(glTextureId);
        mSurfaceTexture.setDefaultBufferSize(1280, 720);
        if (!mPaused) startCamera();
    }

    /** Called by C++ each GL frame to push the latest camera frame into the OES texture. */
    public void updateTexImage() {
        if (mSurfaceTexture != null) mSurfaceTexture.updateTexImage();
    }

    // -----------------------------------------------------------------------
    // Photo capture
    // -----------------------------------------------------------------------

    /**
     * Captures a single JPEG still from the live camera feed.
     *
     * @param outputPath  Absolute file path for the JPEG. If null, a timestamped
     *                    file is created in the app cache directory.
     * @param callback    Fired on the background thread when the image is written.
     */
    public void capturePhoto(String outputPath, CaptureCallback callback) {
        if (mCaptureSession == null || mCameraDevice == null) {
            callback.onError("Camera session not ready");
            return;
        }
        if (outputPath == null) outputPath = defaultPhotoPath();

        mPendingPhotoPath    = outputPath;
        mPendingPhotoCallback = callback;

        try {
            CaptureRequest.Builder builder =
                mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE);
            builder.addTarget(mImageReader.getSurface());
            builder.set(CaptureRequest.CONTROL_AF_MODE,
                        CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);
            mCaptureSession.capture(builder.build(), null, mBackgroundHandler);
        } catch (CameraAccessException e) {
            mPendingPhotoCallback = null;
            callback.onError(e.getMessage());
        }
    }

    // -----------------------------------------------------------------------
    // Video recording
    // -----------------------------------------------------------------------

    /**
     * Starts recording the camera feed to an MP4 file.
     *
     * The Camera2 session is recreated to include the MediaRecorder surface.
     * {@code callback.onSuccess} fires when recording has actually started.
     *
     * Requires {@code android.permission.RECORD_AUDIO} in addition to
     * {@code android.permission.CAMERA}.
     *
     * @param outputPath  Absolute path for the MP4. Null = cache directory.
     * @param callback    Fires on session-configured (success) or immediately on error.
     */
    public void startRecording(String outputPath, CaptureCallback callback) {
        if (mIsRecording) {
            callback.onError("Already recording");
            return;
        }
        if (mCameraDevice == null) {
            callback.onError("Camera not open");
            return;
        }
        if (outputPath == null) outputPath = defaultVideoPath();

        // 1. Prepare MediaRecorder before adding its surface to the session.
        mMediaRecorder = new MediaRecorder();
        mMediaRecorder.setAudioSource(MediaRecorder.AudioSource.MIC);
        mMediaRecorder.setVideoSource(MediaRecorder.VideoSource.SURFACE);
        mMediaRecorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
        mMediaRecorder.setOutputFile(outputPath);
        mMediaRecorder.setVideoEncodingBitRate(10_000_000);
        mMediaRecorder.setVideoFrameRate(30);
        mMediaRecorder.setVideoSize(1280, 720);
        mMediaRecorder.setVideoEncoder(MediaRecorder.VideoEncoder.H264);
        mMediaRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);

        try {
            mMediaRecorder.prepare();
        } catch (IOException e) {
            mMediaRecorder.release();
            mMediaRecorder = null;
            callback.onError("MediaRecorder prepare failed: " + e.getMessage());
            return;
        }

        // 2. Recreate Camera2 session to include the MediaRecorder surface.
        mPendingRecordingPath   = outputPath;
        mRecordingStartCallback = callback;
        mPendingStartRecording  = true;

        reopenCaptureSession();
    }

    /**
     * Stops an in-progress recording.
     *
     * @param callback  Fires with the path of the completed file on success.
     */
    public void stopRecording(CaptureCallback callback) {
        if (!mIsRecording) {
            callback.onError("Not recording");
            return;
        }

        String finishedPath = mPendingRecordingPath;
        safeStopMediaRecorder();

        // Recreate session without the MediaRecorder surface.
        reopenCaptureSession();

        callback.onSuccess(finishedPath);
    }

    public boolean isRecording() { return mIsRecording; }

    // -----------------------------------------------------------------------
    // Camera2 session management (private)
    // -----------------------------------------------------------------------

    private void startCamera() {
        if (mSurfaceTexture == null || mDisposed) return;
        startBackgroundThread();
        // ImageReader for still capture (JPEG, 2-buffer queue so we never stall)
        if (mImageReader == null) {
            mImageReader = ImageReader.newInstance(1280, 720, ImageFormat.JPEG, 2);
            mImageReader.setOnImageAvailableListener(mOnImageAvailable, mBackgroundHandler);
        }
        openCamera();
    }

    private void stopCamera() {
        if (mIsRecording) safeStopMediaRecorder();
        closeSession();
        if (mCameraDevice != null) {
            mCameraDevice.close();
            mCameraDevice = null;
        }
        stopBackgroundThread();
    }

    private void openCamera() {
        Context ctx = mAppContext;
        if (ctx == null) { Log.e(TAG, "openCamera: no application context"); return; }
        if (ContextCompat.checkSelfPermission(ctx, Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "openCamera: CAMERA permission not granted");
            return;
        }
        mCameraManager = (CameraManager) ctx.getSystemService(Context.CAMERA_SERVICE);
        String cameraId = selectCamera();
        if (cameraId == null) return;
        try {
            mCameraManager.openCamera(cameraId, mDeviceCallback, mBackgroundHandler);
        } catch (CameraAccessException | SecurityException e) {
            Log.e(TAG, "openCamera failed: " + e.getMessage());
        }
    }

    /** Closes only the session, leaving the device open for session recreation. */
    private void closeSession() {
        if (mCaptureSession != null) {
            try { mCaptureSession.stopRepeating(); } catch (Exception ignored) {}
            mCaptureSession.close();
            mCaptureSession = null;
        }
    }

    /** Closes session and reopens it — used when toggling recording. */
    private void reopenCaptureSession() {
        closeSession();
        if (mCameraDevice != null) createCaptureSession();
    }

    private String selectCamera() {
        try {
            for (String id : mCameraManager.getCameraIdList()) {
                CameraCharacteristics ch = mCameraManager.getCameraCharacteristics(id);
                Integer facing = ch.get(CameraCharacteristics.LENS_FACING);
                if (facing == null) continue;
                boolean isFront = (facing == CameraCharacteristics.LENS_FACING_FRONT);
                if (isFront == mFrontFacing) return id;
            }
            String[] ids = mCameraManager.getCameraIdList();
            return ids.length > 0 ? ids[0] : null;
        } catch (CameraAccessException e) {
            Log.e(TAG, "selectCamera: " + e.getMessage());
            return null;
        }
    }

    private final CameraDevice.StateCallback mDeviceCallback = new CameraDevice.StateCallback() {
        @Override public void onOpened(@NonNull CameraDevice camera) {
            mCameraDevice = camera;
            createCaptureSession();
        }
        @Override public void onDisconnected(@NonNull CameraDevice camera) {
            camera.close(); mCameraDevice = null;
        }
        @Override public void onError(@NonNull CameraDevice camera, int error) {
            Log.e(TAG, "CameraDevice error: " + error);
            camera.close(); mCameraDevice = null;
        }
    };

    private void createCaptureSession() {
        if (mCameraDevice == null || mSurfaceTexture == null) return;
        try {
            Surface previewSurface = new Surface(mSurfaceTexture);

            // Always include the preview and photo surfaces.
            List<Surface> surfaces = new ArrayList<>();
            surfaces.add(previewSurface);
            if (mImageReader != null) surfaces.add(mImageReader.getSurface());
            // Include the MediaRecorder surface only while recording.
            if (mPendingStartRecording && mMediaRecorder != null) {
                surfaces.add(mMediaRecorder.getSurface());
            }

            // Preview repeating request targets preview (+ recorder when recording).
            CaptureRequest.Builder previewBuilder =
                mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
            previewBuilder.addTarget(previewSurface);
            if (mPendingStartRecording && mMediaRecorder != null) {
                previewBuilder.addTarget(mMediaRecorder.getSurface());
            }
            previewBuilder.set(CaptureRequest.CONTROL_AF_MODE,
                               CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);

            final boolean shouldStartRecorder = mPendingStartRecording;
            final String  recordingPath       = mPendingRecordingPath;
            final CaptureCallback startCb     = mRecordingStartCallback;

            mCameraDevice.createCaptureSession(surfaces,
                new CameraCaptureSession.StateCallback() {
                    @Override
                    public void onConfigured(@NonNull CameraCaptureSession session) {
                        if (mCameraDevice == null) return;
                        mCaptureSession = session;
                        try {
                            mCaptureSession.setRepeatingRequest(
                                previewBuilder.build(), mCaptureCallback, mBackgroundHandler);
                        } catch (CameraAccessException e) {
                            Log.e(TAG, "setRepeatingRequest: " + e.getMessage());
                        }

                        // Start the MediaRecorder now that the session is live.
                        if (shouldStartRecorder && mMediaRecorder != null) {
                            mMediaRecorder.start();
                            mIsRecording           = true;
                            mPendingStartRecording = false;
                            mRecordingStartCallback = null;
                            if (startCb != null) startCb.onSuccess(recordingPath);
                        }
                    }
                    @Override
                    public void onConfigureFailed(@NonNull CameraCaptureSession session) {
                        Log.e(TAG, "CaptureSession config failed");
                        if (shouldStartRecorder && startCb != null) {
                            startCb.onError("Session configuration failed");
                        }
                    }
                }, mBackgroundHandler);
        } catch (CameraAccessException e) {
            Log.e(TAG, "createCaptureSession: " + e.getMessage());
        }
    }

    private final CameraCaptureSession.CaptureCallback mCaptureCallback =
        new CameraCaptureSession.CaptureCallback() {
            @Override
            public void onCaptureStarted(@NonNull CameraCaptureSession session,
                                         @NonNull CaptureRequest request,
                                         long timestamp, long frameNumber) {
                onFirstFrame();
            }
        };

    // -----------------------------------------------------------------------
    // ImageReader — JPEG still capture
    // -----------------------------------------------------------------------

    private final ImageReader.OnImageAvailableListener mOnImageAvailable =
        reader -> {
            Image image = reader.acquireLatestImage();
            if (image == null) return;

            ByteBuffer buffer = image.getPlanes()[0].getBuffer();
            byte[] bytes = new byte[buffer.remaining()];
            buffer.get(bytes);
            image.close();

            String path = mPendingPhotoPath;
            CaptureCallback cb = mPendingPhotoCallback;
            mPendingPhotoPath    = null;
            mPendingPhotoCallback = null;

            if (path == null || cb == null) return;

            try {
                File file = new File(path);
                //noinspection ResultOfMethodCallIgnored
                file.getParentFile().mkdirs();
                try (FileOutputStream fos = new FileOutputStream(file)) {
                    fos.write(bytes);
                }
                cb.onSuccess(path);
            } catch (IOException e) {
                cb.onError("Failed to write photo: " + e.getMessage());
            }
        };

    // -----------------------------------------------------------------------
    // MediaRecorder helpers
    // -----------------------------------------------------------------------

    private void safeStopMediaRecorder() {
        if (mMediaRecorder == null) return;
        try { mMediaRecorder.stop(); } catch (Exception ignored) {}
        mMediaRecorder.reset();
        mMediaRecorder.release();
        mMediaRecorder     = null;
        mIsRecording       = false;
        mPendingRecordingPath = null;
    }

    // -----------------------------------------------------------------------
    // First-frame callback
    // -----------------------------------------------------------------------

    private void onFirstFrame() {
        if (!mFirstFrameFired) {
            mFirstFrameFired = true;
            if (mReadyListener != null) mReadyListener.onCameraReady();
        }
    }

    // -----------------------------------------------------------------------
    // Background thread
    // -----------------------------------------------------------------------

    private void startBackgroundThread() {
        if (mBackgroundThread != null) return;
        mBackgroundThread = new HandlerThread("CameraTextureThread");
        mBackgroundThread.start();
        mBackgroundHandler = new Handler(mBackgroundThread.getLooper());
    }

    private void stopBackgroundThread() {
        if (mBackgroundThread == null) return;
        mBackgroundThread.quitSafely();
        try { mBackgroundThread.join(); } catch (InterruptedException ignored) {}
        mBackgroundThread  = null;
        mBackgroundHandler = null;
    }

    // -----------------------------------------------------------------------
    // Default output paths
    // -----------------------------------------------------------------------

    private String defaultPhotoPath() {
        Context ctx = mAppContext;
        File dir = (ctx != null) ? ctx.getCacheDir() : new File("/sdcard");
        return new File(dir, "viro_photo_" + System.currentTimeMillis() + ".jpg")
                .getAbsolutePath();
    }

    private String defaultVideoPath() {
        Context ctx = mAppContext;
        File dir = (ctx != null) ? ctx.getCacheDir() : new File("/sdcard");
        return new File(dir, "viro_video_" + System.currentTimeMillis() + ".mp4")
                .getAbsolutePath();
    }

    // -----------------------------------------------------------------------
    // JNI — implemented in CameraTexture_JNI.cpp
    // -----------------------------------------------------------------------

    private native long nativeCreate(boolean isFront);
    private native void nativeInit(long nativeRef, long viroContextRef);
    private native void nativePlay(long nativeRef);
    private native void nativePause(long nativeRef);
    private native void nativeDispose(long nativeRef);
}
