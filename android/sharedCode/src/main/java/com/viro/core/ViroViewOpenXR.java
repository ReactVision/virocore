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
import android.app.Application;
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

import java.lang.ref.WeakReference;
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
    private Application mApplication; // for unregistering ActivityLifecycleCallbacks
    private boolean mResumed = false;  // tracks renderer.onResume / onPause balance

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

        // Native renderer creation (xrCreateInstance + xrCreateSession) is deferred
        // until the host Activity is known. The Activity passed to this constructor
        // is reactContext.getCurrentActivity(), which can be the *outer* host
        // (e.g. MainActivity) when this view is mounted during a different Activity's
        // onCreate (e.g. VRActivity in the dual-activity Quest setup). Binding
        // xrCreateSession to the wrong Activity leaves the OpenXR session stuck in
        // IDLE the moment that Activity pauses. We resolve the true host on the
        // first onActivityResumed whose Activity owns this view's window.

        // Register directly with the Application's ActivityLifecycleCallbacks.
        // The default ViroView lifecycle plumbing relies on
        // VRT3DSceneNavigator's React LifecycleEventListener (onHostResume),
        // which does NOT re-fire when a second Activity resumes against an
        // already-RESUMED ReactHost — exactly the case here, where MainActivity
        // resumed first and VRActivity's onHostResume is a no-op transition.
        // Subscribing to Application lifecycle ensures we still see VRActivity's
        // onResume directly from Android, independent of React's state machine.
        mApplication = (Application) activity.getApplicationContext();
        mApplication.registerActivityLifecycleCallbacks(this);
    }

    /**
     * Lazy-create the native renderer. Called once, from the first
     * {@link #onActivityResumed(Activity)} whose Activity hosts this view.
     */
    private void initRenderer(final Activity activity) {
        if (mNativeRenderer != null || mDestroyed) {
            return;
        }

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

        // If a Scene was set before the renderer was ready, apply it now.
        if (mCurrentScene != null) {
            mNativeRenderer.setSceneController(mCurrentScene.mNativeRef, 0.5f);
        }

        // Notify caller that the renderer is ready. Posted async to keep the
        // contract used by all ViroViews (onSuccess fires from a follow-up tick,
        // never synchronously inside the constructor / lifecycle callback).
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

    // ── View attachment lifecycle ────────────────────────────────────────────────

    /**
     * Initialize the native renderer when this view is attached to its window.
     *
     * Why here (and not in onActivityResumed):
     *
     *   - VRActivity's onResume fires before Fabric processes its mount items.
     *     ViroViewOpenXR is constructed *during* mount, i.e. AFTER VRActivity's
     *     onResume → Application.ActivityLifecycleCallbacks.onActivityResumed
     *     has already been delivered to all subscribers. We can't replay past
     *     events. By the time we'd subscribe, the resume has come and gone.
     *
     *   - onAttachedToWindow runs after our View has joined the window's view
     *     tree — at which point getRootView() walks up to VRActivity's DecorView
     *     and we can recover the host Activity unambiguously.
     *
     * Subsequent lifecycle events (pause/stop/resume after headset off/on) are
     * still delivered via the Application.ActivityLifecycleCallbacks registered
     * in init(), and routed to the renderer by onActivityPaused/Stopped/Resumed.
     */
    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (mNativeRenderer != null || mDestroyed) {
            return;
        }
        Activity host = findHostActivity();
        if (host == null) {
            android.util.Log.w("VRORendererOpenXR",
                "onAttachedToWindow: could not resolve host Activity; deferring");
            return;
        }
        android.util.Log.i("VRORendererOpenXR",
            "onAttachedToWindow: host = " + host.getClass().getSimpleName()
                + " (" + System.identityHashCode(host) + ")");
        mWeakActivity = new WeakReference<>(host);
        initRenderer(host);
        // VRActivity already fired onResume before we attached; the renderer
        // needs an explicit resume to start its render thread. Set mResumed
        // so the later RN LifecycleEventListener.onHostResume (which also
        // calls our onActivityResumed) doesn't try to start a *second* render
        // thread — std::thread reassignment would terminate the process.
        if (mNativeRenderer != null && !mResumed) {
            mResumed = true;
            mNativeRenderer.onResume();
        }
    }

    /**
     * Resolve the Activity that should own the OpenXR session.
     *
     * In dual-Activity Quest setups, both MainActivity and VRActivity can be
     * RESUMED simultaneously (Android 10+ multi-resumed mode). Heuristics like
     * "first resumed" or "first with focus" are ambiguous in that window. The
     * unambiguous signal is the window token: this view is attached to exactly
     * one window, and that window belongs to exactly one Activity. Match.
     *
     * Tries, in order:
     *   1. Match by window token — find the Activity whose DecorView has the
     *      same window token as `this`. Only one Activity will match.
     *   2. The root view's context (works in single-Activity apps).
     *   3. Our own constructor-time context (fallback).
     */
    private Activity findHostActivity() {
        android.os.IBinder ourToken = getWindowToken();
        if (ourToken != null) {
            for (Activity a : getAllAliveActivities()) {
                try {
                    android.view.Window window = a.getWindow();
                    if (window == null) continue;
                    View decor = window.getDecorView();
                    if (decor != null && decor.getWindowToken() == ourToken) {
                        return a;
                    }
                } catch (Throwable ignored) {}
            }
        }
        View root = getRootView();
        Context ctx = (root != null) ? root.getContext() : getContext();
        Activity host = ContextUtils.getActivity(ctx);
        if (host != null) {
            return host;
        }
        return ContextUtils.getActivity(getContext());
    }

    /**
     * All non-finishing, non-destroyed Activities in this process, via
     * {@code android.app.ActivityThread.mActivities} reflection. Internal API
     * but de-facto stable across Android versions; failure returns empty.
     */
    private static java.util.List<Activity> getAllAliveActivities() {
        java.util.List<Activity> result = new java.util.ArrayList<>();
        try {
            Class<?> threadCls = Class.forName("android.app.ActivityThread");
            Object thread = threadCls.getMethod("currentActivityThread").invoke(null);
            java.lang.reflect.Field activitiesField = threadCls.getDeclaredField("mActivities");
            activitiesField.setAccessible(true);
            Object activities = activitiesField.get(thread);
            if (!(activities instanceof java.util.Map)) return result;
            for (Object record : ((java.util.Map<?, ?>) activities).values()) {
                java.lang.reflect.Field activityField = record.getClass().getDeclaredField("activity");
                activityField.setAccessible(true);
                Object a = activityField.get(record);
                if (!(a instanceof Activity)) continue;
                Activity act = (Activity) a;
                if (act.isFinishing() || act.isDestroyed()) continue;
                result.add(act);
            }
        } catch (Throwable ignored) {}
        return result;
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
        if (mNativeRenderer != null) {
            mNativeRenderer.recenterTracking();
        }
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
        if (mNativeRenderer != null) {
            mNativeRenderer.setPassthroughEnabled(enabled);
        }
    }

    /**
     * Enable or disable skeletal hand tracking (XR_EXT_hand_tracking).
     * Hand tracking is auto-detected at session creation; this toggles whether
     * gesture events are processed each frame. No-op if the extension is absent.
     *
     * @param enabled {@code true} to process hand gestures; {@code false} to suppress them.
     */
    public void setHandTrackingEnabled(boolean enabled) {
        if (mNativeRenderer != null) {
            mNativeRenderer.setHandTrackingEnabled(enabled);
        }
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
        // If the renderer is not yet initialized (waiting for host Activity to
        // resume), the scene is applied later inside initRenderer().
        if (mNativeRenderer != null) {
            mNativeRenderer.setSceneController(scene.mNativeRef, 0.5f);
        }
    }

    // ── Renderer-config setters ──────────────────────────────────────────────────
    // These are called by Fabric during view mount (via VRT3DSceneNavigator props),
    // which can run before the native Renderer is created (we defer xrCreateSession
    // to the host Activity's first onActivityResumed). Cache the value on
    // mRendererConfig so it is picked up when the Renderer is constructed in
    // initRenderer(); only forward to super (which calls into the native renderer)
    // once mNativeRenderer is non-null.

    @Override
    public void setShadowsEnabled(boolean enabled) {
        if (mRendererConfig != null) mRendererConfig.setShadowsEnabled(enabled);
        if (mNativeRenderer != null) super.setShadowsEnabled(enabled);
    }

    @Override
    public void setHDREnabled(boolean enabled) {
        if (mRendererConfig != null) mRendererConfig.setHDREnabled(enabled);
        if (mNativeRenderer != null) super.setHDREnabled(enabled);
    }

    @Override
    public void setPBREnabled(boolean enabled) {
        if (mRendererConfig != null) mRendererConfig.setPBREnabled(enabled);
        if (mNativeRenderer != null) super.setPBREnabled(enabled);
    }

    @Override
    public void setBloomEnabled(boolean enabled) {
        if (mRendererConfig != null) mRendererConfig.setBloomEnabled(enabled);
        if (mNativeRenderer != null) super.setBloomEnabled(enabled);
    }

    /** @hide */
    @Override
    public void setDebug(boolean debug) {
        // no-op — debug HUD not used on Quest; setDebugHUDEnabled() inherited from
        // ViroView is null-guarded indirectly by VRT3DSceneNavigator only calling
        // it after construction. Override here to be safe.
        // (setDebugHUDEnabled in the parent is final, so we can't override it
        // directly — the parent's null-deref is gated by the VRT delegate.)
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
        if (mWeakActivity.get() != activity || mNativeRenderer == null) {
            return;
        }
        mNativeRenderer.onStart();
    }

    /** @hide */
    @Override
    public void onActivityResumed(Activity activity) {
        // First-time init: bind to the Activity that actually hosts this view.
        // The Activity passed to the constructor may have been the wrong one
        // (e.g. MainActivity for a view mounted into VRActivity in dual-activity
        // Quest setups). The host is identified by its window owning our root.
        if (mNativeRenderer == null) {
            if (!isAttachedToWindow()
                    || activity.getWindow() == null
                    || activity.getWindow().getDecorView() != getRootView()) {
                return;
            }
            mWeakActivity = new WeakReference<>(activity);
            initRenderer(activity);
        }
        if (mWeakActivity.get() != activity || mNativeRenderer == null) {
            return;
        }
        if (mResumed) {
            return;  // already running; native onResume is idempotent but skip the JNI hop
        }
        mResumed = true;
        // onResume starts the OpenXR render thread in C++.
        mNativeRenderer.onResume();
    }

    /** @hide */
    @Override
    public void onActivityPaused(Activity activity) {
        if (mWeakActivity.get() != activity || mNativeRenderer == null) {
            return;
        }
        if (!mResumed) {
            return;
        }
        mResumed = false;
        mNativeRenderer.onPause();
    }

    /** @hide */
    @Override
    public void onActivityStopped(Activity activity) {
        if (mWeakActivity.get() != activity || mNativeRenderer == null) {
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
        // Only dispose when our host Activity is destroyed. We are now registered
        // with the Application's ActivityLifecycleCallbacks so we receive
        // onActivityDestroyed for *every* Activity in the process — including
        // MainActivity, which may finish independently of VRActivity in the
        // dual-activity setup. Disposing on the wrong Activity would tear down
        // the OpenXR session while VR is still running.
        if (mWeakActivity.get() != activity) {
            return;
        }
        this.dispose();
    }

    /** @hide */
    @Override
    public void dispose() {
        if (mApplication != null) {
            mApplication.unregisterActivityLifecycleCallbacks(this);
            mApplication = null;
        }
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
        if (mNativeRenderer != null) {
            mNativeRenderer.onKeyEvent(event.getKeyCode(), event.getAction());
        }
        return true;
    }

    /** @hide */
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (mNativeRenderer != null) {
            mNativeRenderer.onTouchEvent(event.getAction(), event.getX(), event.getY());
        }
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
