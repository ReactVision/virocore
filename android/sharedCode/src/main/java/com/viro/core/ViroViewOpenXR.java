//
//  ViroViewOpenXR.java
//  ViroCore
//
//  Java bootstrap for Meta Quest rendering via the Khronos OpenXR API.
//  Replaces ViroViewOVR (VrApi) which is not supported on Quest 3 and newer.
//
//  Key difference from all other ViroViews: the native Renderer is instantiated
//  in the constructor, not in surfaceCreated(), because OpenXR manages its own
//  display compositor — there is no Android Surface involved.
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  MIT License — see LICENSE file.

package com.viro.core;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
import androidx.annotation.AttrRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import com.google.vr.cardboard.ContextUtils;
import com.viro.core.internal.PlatformUtil;
import com.viro.core.internal.RenderCommandQueue;

import android.os.Handler;
import android.os.Looper;

import java.util.List;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * ViroViewOpenXR is a {@link ViroView} for rendering content in stereo VR on Meta Quest
 * headsets using the Khronos OpenXR API. Supports Quest 2, Quest 3, Quest 3S, and Quest Pro.
 *
 * Unlike other ViroViews there is no Android Surface or SurfaceView — OpenXR owns the
 * display compositor and manages its own EGL context and swapchain.
 */
public class ViroViewOpenXR extends ViroView {

    static {
        System.loadLibrary("viro_renderer");
    }

    /**
     * Callback interface for responding to {@link ViroViewOpenXR} startup success or failure.
     */
    public interface StartupListener {

        /**
         * Invoked when ViroViewOpenXR has finished initializing the OpenXR instance and session.
         * At this point the view is ready to receive a {@link Scene}.
         */
        void onSuccess();

        /**
         * Invoked when ViroViewOpenXR failed to initialize.
         *
         * @param error        The error code.
         * @param errorMessage Human-readable reason for the failure.
         */
        void onFailure(StartupError error, String errorMessage);
    }

    /**
     * Error codes for {@link StartupListener#onFailure}.
     */
    public enum StartupError {
        /**
         * OpenXR instance or session creation failed. Check logcat for XrResult codes.
         */
        OPENXR_INIT_FAILED,
        /**
         * Unknown error.
         */
        UNKNOWN,
    }

    /**
     * Executes events on the render thread by collecting them in a queue and draining
     * on each {@code onDrawFrame()} callback from native code.
     */
    static class OpenXRRenderCommandQueue implements RenderCommandQueue, FrameListener {

        private Queue<Runnable> mQueue = new ConcurrentLinkedQueue<>();

        @Override
        public void queueEvent(Runnable r) {
            mQueue.add(r);
        }

        @Override
        public void onDrawFrame() {
            Runnable r;
            while ((r = mQueue.poll()) != null) {
                r.run();
            }
        }
    }

    private AssetManager mAssetManager;
    private OpenXRRenderCommandQueue mRenderQueue = new OpenXRRenderCommandQueue();
    private List<FrameListener> mFrameListeners = new CopyOnWriteArrayList<>();
    private PlatformUtil mPlatformUtil;
    private StartupListener mStartupListener;

    // ── Constructors ─────────────────────────────────────────────────────────────

    /**
     * Create a new ViroViewOpenXR with the default {@link RendererConfiguration}.
     *
     * @param activity        The activity containing this view.
     * @param startupListener Optional listener for startup success/failure. May be null.
     */
    public ViroViewOpenXR(final Activity activity, final StartupListener startupListener) {
        super(activity, null);
        init(startupListener);
    }

    /**
     * Create a new ViroViewOpenXR with an explicit {@link RendererConfiguration}.
     *
     * @param activity        The activity containing this view.
     * @param startupListener Optional listener. May be null.
     * @param config          Renderer configuration (shadows, HDR, PBR, bloom).
     */
    public ViroViewOpenXR(final Activity activity, final StartupListener startupListener,
                          RendererConfiguration config) {
        super(activity, config);
        init(startupListener);
    }

    /** @hide */
    public ViroViewOpenXR(@NonNull final Context context) {
        this(context, (AttributeSet) null);
    }

    /** @hide */
    public ViroViewOpenXR(@NonNull final Context context, @Nullable final AttributeSet attrs) {
        this(context, attrs, 0);
    }

    /** @hide */
    public ViroViewOpenXR(@NonNull final Context context, @Nullable final AttributeSet attrs,
                          @AttrRes final int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        if (ContextUtils.getActivity(context) == null) {
            throw new IllegalArgumentException("An Activity Context is required for Viro functionality.");
        }
        init(null);
    }

    // ── Initialisation ───────────────────────────────────────────────────────────

    private void init(final StartupListener startupListener) {
        Activity activity = mWeakActivity.get();
        if (activity == null) {
            return;
        }

        // OpenXR owns the display — add an invisible placeholder so FrameLayout
        // can measure itself. No SurfaceView is needed.
        addView(new View(activity));

        mAssetManager = getResources().getAssets();
        mPlatformUtil = new PlatformUtil(mRenderQueue, mFrameListeners,
                                         activity.getApplicationContext(), mAssetManager);
        mFrameListeners.add(mRenderQueue);
        mStartupListener = startupListener;

        // Instantiate the native renderer immediately — OpenXR does not wait for
        // a surface event. The C++ constructor calls xrCreateInstance + xrCreateSession.
        // `this` is passed so C++ can call back onDrawFrame() each frame to drain
        // the Java render-thread queue (FrameListeners, PlatformUtil callbacks).
        mNativeRenderer = new Renderer(
                getClass().getClassLoader(),
                activity.getApplicationContext(),
                this,
                activity,
                mAssetManager,
                mPlatformUtil,
                mRendererConfig);

        mNativeViroContext = new ViroContext(mNativeRenderer.mNativeRef);

        // Notify caller that the renderer is ready.
        // Post to the main looper so onSuccess() always fires AFTER the constructor
        // returns — callers (e.g. MainActivity) assign the view reference in the same
        // expression that calls the constructor, so a synchronous callback would see
        // a null reference. All other ViroViews fire onSuccess from their render thread
        // (always async); this matches that contract.
        if (mStartupListener != null && !mDestroyed) {
            final StartupListener listener = mStartupListener;
            new Handler(Looper.getMainLooper()).post(() -> {
                if (!mDestroyed) {
                    listener.onSuccess();
                }
            });
        }

        activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    // ── ViroView overrides ────────────────────────────────────────────────────────

    @Override
    protected int getSystemUiVisibilityFlags() {
        return (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    public void recenterTracking() {
        mNativeRenderer.recenterTracking();
    }

    /**
     * Enable or disable XR_FB_passthrough mixed-reality mode.
     * On Quest 3 / Quest Pro the camera feed is shown behind virtual content.
     * On Quest 2 the feed is grayscale. No-op on devices that do not support
     * the XR_FB_passthrough extension.
     *
     * @param enabled {@code true} to show passthrough; {@code false} for fully virtual.
     */
    public void setPassthroughEnabled(boolean enabled) {
        mNativeRenderer.setPassthroughEnabled(enabled);
    }

    @Override
    public ViroMediaRecorder getRecorder() {
        return null; // Not supported on Quest.
    }

    @Override
    public void setScene(Scene scene) {
        if (scene == mCurrentScene) {
            return;
        }
        super.setScene(scene);
        mNativeRenderer.setSceneController(scene.mNativeRef, 0.5f);
    }

    /** @hide */
    @Override
    public void setDebug(boolean debug) {
        // no-op
    }

    /** @hide */
    @Override
    public void setVRModeEnabled(boolean vrModeEnabled) {
        // no-op — OpenXR is always in VR mode
    }

    /** @hide */
    @Override
    public String getPlatform() {
        return "openxr";
    }

    // ── Activity lifecycle ────────────────────────────────────────────────────────

    /** @hide */
    @Override
    public void onActivityCreated(Activity activity, Bundle bundle) {
        // no-op
    }

    /** @hide */
    @Override
    public void onActivityStarted(Activity activity) {
        if (mWeakActivity.get() != activity) {
            return;
        }
        mNativeRenderer.onStart();
    }

    /** @hide */
    @Override
    public void onActivityResumed(Activity activity) {
        if (mWeakActivity.get() != activity) {
            return;
        }
        // onResume starts the OpenXR render thread in C++.
        mNativeRenderer.onResume();
    }

    /** @hide */
    @Override
    public void onActivityPaused(Activity activity) {
        if (mWeakActivity.get() != activity) {
            return;
        }
        mNativeRenderer.onPause();
    }

    /** @hide */
    @Override
    public void onActivityStopped(Activity activity) {
        if (mWeakActivity.get() != activity) {
            return;
        }
        // onStop joins the render thread in C++.
        mNativeRenderer.onStop();
    }

    /** @hide */
    @Override
    public void onActivitySaveInstanceState(Activity activity, Bundle bundle) {
        // no-op
    }

    /** @hide */
    @Override
    public void onActivityDestroyed(Activity activity) {
        this.dispose();
    }

    /** @hide */
    @Override
    public void dispose() {
        super.dispose();
    }

    // ── Input ─────────────────────────────────────────────────────────────────────

    /** @hide */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_UP
                || event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_DOWN) {
            return true;
        }
        mNativeRenderer.onKeyEvent(event.getKeyCode(), event.getAction());
        return true;
    }

    /** @hide */
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        mNativeRenderer.onTouchEvent(event.getAction(), event.getX(), event.getY());
        return true;
    }

    // ── Callbacks from native (VROSceneRendererOpenXR) ─────────────────────────────

    /**
     * Called by native code each frame on the render thread so that queued
     * Java callbacks (frame listeners) are executed on the correct thread.
     *
     * @hide
     */
    void onDrawFrame() {
        for (FrameListener listener : mFrameListeners) {
            listener.onDrawFrame();
        }
    }
}
