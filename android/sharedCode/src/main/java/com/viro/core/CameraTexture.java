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
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY IS OF ANY KIND,
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
 *      as a VROFrameListener, then calls back to Java via nativeOnGLReady(glTextureId).
 *   4. onGLReady() creates SurfaceTexture(glTextureId) and opens the Camera2 session.
 *   5. VROCameraTextureAndroid::onFrameWillRender() calls updateTexImage() each GL frame.
 *
 * Usage (from VRTCameraTexture bridge):
 *   CameraTexture ct = new CameraTexture(viroContext, Position.FRONT);
 *   material.setDiffuseTexture(ct);           // binds OES texture to VROMaterial
 *   ct.setReadyListener(listener);            // fires once on first frame
 *   ct.play();
 */
package com.viro.core;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;

import java.util.Collections;

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

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    private final boolean mFrontFacing;
    private SurfaceTexture mSurfaceTexture;

    private CameraManager mCameraManager;
    private CameraDevice mCameraDevice;
    private CameraCaptureSession mCaptureSession;

    private HandlerThread mBackgroundThread;
    private Handler mBackgroundHandler;

    private volatile boolean mPaused = true;
    private volatile boolean mDisposed = false;
    private boolean mFirstFrameFired = false;

    private ReadyListener mReadyListener;

    // -----------------------------------------------------------------------
    // Constructor — mirrors VideoTexture(ViroContext, ...)
    // -----------------------------------------------------------------------

    /**
     * Creates a CameraTexture that streams live camera frames into a VROMaterial.
     *
     * @param viroContext  The active ViroContext (provides VRODriver / GL thread access).
     * @param position     Which camera to use.
     */
    public CameraTexture(ViroContext viroContext, Position position) {
        mFrontFacing = (position == Position.FRONT);

        // 1. Create C++ VROCameraTextureAndroid; sets mNativeRef.
        mNativeRef = nativeCreate(mFrontFacing);

        // 2. Dispatch initCamera() to the GL thread.
        //    C++ will call back onGLReady(glTextureId) once the OES texture is created.
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
        if (mSurfaceTexture != null) {
            startCamera();
        }
        // If SurfaceTexture not ready yet, startCamera() is called from onGLReady().
        nativePlay(mNativeRef);
    }

    public void pause() {
        mPaused = true;
        stopCamera();
        nativePause(mNativeRef);
    }

    public boolean isPaused() {
        return mPaused;
    }

    @Override
    public void dispose() {
        if (mDisposed) return;
        mDisposed = true;
        stopCamera();
        stopBackgroundThread();
        if (mSurfaceTexture != null) {
            mSurfaceTexture.release();
            mSurfaceTexture = null;
        }
        nativeDispose(mNativeRef);
        mNativeRef = 0;
    }

    // -----------------------------------------------------------------------
    // GL-thread callback from VROCameraTextureAndroid::initCamera()
    // -----------------------------------------------------------------------

    /**
     * Called by C++ (on any thread) after initCamera() has created the OES texture.
     * Sets up SurfaceTexture and opens Camera2 if play() was already called.
     */
    public void onGLReady(int glTextureId) {
        mSurfaceTexture = new SurfaceTexture(glTextureId);
        mSurfaceTexture.setDefaultBufferSize(1280, 720);

        if (!mPaused) {
            startCamera();
        }
    }

    /**
     * Called by C++ VROCameraTextureAndroid::onFrameWillRender() (on the GL thread)
     * each frame to pull the latest camera frame into the OES texture.
     */
    public void updateTexImage() {
        if (mSurfaceTexture != null) {
            mSurfaceTexture.updateTexImage();
        }
    }

    /**
     * Called by Camera2 (via mCaptureCallback) when the first frame is captured.
     * Fires ReadyListener once.
     */
    private void onFirstFrame() {
        if (!mFirstFrameFired) {
            mFirstFrameFired = true;
            if (mReadyListener != null) {
                mReadyListener.onCameraReady();
            }
        }
    }

    // -----------------------------------------------------------------------
    // Camera2 session management
    // -----------------------------------------------------------------------

    private void startCamera() {
        if (mSurfaceTexture == null || mDisposed) return;
        startBackgroundThread();
        openCamera();
    }

    private void stopCamera() {
        closeCamera();
        stopBackgroundThread();
    }

    private void openCamera() {
        Context ctx = ViroContext.getApplicationContext();
        if (ctx == null) {
            Log.e(TAG, "openCamera: no application context");
            return;
        }
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

    private void closeCamera() {
        if (mCaptureSession != null) {
            try { mCaptureSession.stopRepeating(); } catch (Exception ignored) {}
            mCaptureSession.close();
            mCaptureSession = null;
        }
        if (mCameraDevice != null) {
            mCameraDevice.close();
            mCameraDevice = null;
        }
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
        try {
            Surface surface = new Surface(mSurfaceTexture);
            CaptureRequest.Builder builder =
                mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            builder.addTarget(surface);
            builder.set(CaptureRequest.CONTROL_AF_MODE,
                        CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);

            mCameraDevice.createCaptureSession(
                Collections.singletonList(surface),
                new CameraCaptureSession.StateCallback() {
                    @Override
                    public void onConfigured(@NonNull CameraCaptureSession session) {
                        if (mCameraDevice == null) return;
                        mCaptureSession = session;
                        try {
                            mCaptureSession.setRepeatingRequest(
                                builder.build(), mCaptureCallback, mBackgroundHandler);
                        } catch (CameraAccessException e) {
                            Log.e(TAG, "setRepeatingRequest: " + e.getMessage());
                        }
                    }
                    @Override
                    public void onConfigureFailed(@NonNull CameraCaptureSession session) {
                        Log.e(TAG, "CaptureSession config failed");
                    }
                },
                mBackgroundHandler);
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
        mBackgroundThread = null;
        mBackgroundHandler = null;
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
