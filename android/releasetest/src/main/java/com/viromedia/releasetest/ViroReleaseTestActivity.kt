package com.viromedia.releasetest

import android.content.res.Configuration
import androidx.appcompat.app.AppCompatActivity
import com.viro.core.RendererCloseListener
import com.viro.core.ViroView
import android.os.Bundle
import android.util.Log
import android.view.View
import com.viro.core.ViroViewGVR
import com.viro.core.ViroViewARCore
import android.view.WindowManager
import android.widget.ImageView
import com.viro.core.ViroViewScene

class ViroReleaseTestActivity : AppCompatActivity(), RendererCloseListener {

    lateinit var viroView: ViroView

    private var thumbsUp: ImageView? = null
    private var thumbsDown: ImageView? = null
    override fun onCreate(savedInstanceState: Bundle?) {
        Log.i(TAG, "onCreate")
        super.onCreate(savedInstanceState)
        if (BuildConfig.VR_PLATFORM.equals("GVR", ignoreCase = true)) {
            if (BuildConfig.VR_ENABLED == 1) {
                setContentView(R.layout.activity_main_gvr_vr_enabled)
            }
            if (BuildConfig.VR_ENABLED == 0) {
                setContentView(R.layout.activity_main_gvr_vr_disabled)
            }
        } else if (BuildConfig.VR_PLATFORM.equals("OVR", ignoreCase = true)) {
            setContentView(R.layout.activity_main_ovr)
        } else if (BuildConfig.VR_PLATFORM.equals("ARCore", ignoreCase = true)) {
            setContentView(R.layout.activity_main_arcore)
        } else if (BuildConfig.VR_PLATFORM.equals("Scene", ignoreCase = true)) {
            setContentView(R.layout.activity_main_scene)
        }
        viroView = findViewById<View>(R.id.viro_view) as ViroView
        if (BuildConfig.VR_ENABLED == 0) {
            thumbsUp = findViewById<View>(R.id.thumbsUp) as ImageView
            thumbsDown = findViewById<View>(R.id.thumbsDown) as ImageView
        }
    }

    fun startRenderer() {
        when {
            BuildConfig.VR_PLATFORM.equals("GVR", ignoreCase = true) -> {
                (viroView as ViroViewGVR?)!!.startTests()
                (viroView as ViroViewGVR?)!!.setVRExitRunnable { Log.d(TAG, "On GVR userRequested exit") }
                viroView.setVRModeEnabled(BuildConfig.VR_ENABLED == 1)
            }
            BuildConfig.VR_PLATFORM.equals("ARCore", ignoreCase = true) -> {
                (viroView as ViroViewARCore?)!!.startTests()
                val arView = viroView as ViroViewARCore?
                val display = (getSystemService(WINDOW_SERVICE) as WindowManager).defaultDisplay
                arView!!.setCameraRotation(display.rotation)
            }
            BuildConfig.VR_PLATFORM.equals("Scene", ignoreCase = true) -> {
                (viroView as ViroViewScene?)!!.startTests()
            }
        }
    }

    val thumbsUpView: View?
        get() = thumbsUp
    val thumbsDownView: View?
        get() = thumbsDown

    override fun onStart() {
        Log.i(TAG, "onStart")
        super.onStart()
    }

    override fun onResume() {
        Log.i(TAG, "onResume")
        super.onResume()
        viroView.onActivityStarted(this)
        viroView.onActivityResumed(this)
    }

    override fun onPause() {
        Log.i(TAG, "onPause")
        super.onPause()
        viroView.onActivityPaused(this)
        viroView.onActivityStopped(this)
        viroView.onActivityDestroyed(this)
        viroView.dispose()
    }

    override fun onStop() {
        Log.i(TAG, "onStop")
        super.onStop()
    }

    override fun onDestroy() {
        Log.i(TAG, "onDestroy")
        super.onDestroy()
    }

    override fun onRendererClosed() {
        Log.d(TAG, "On GVR userRequested exit")
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (viroView is ViroViewARCore) {
            val arView = viroView as ViroViewARCore
            val display = (getSystemService(WINDOW_SERVICE) as WindowManager).defaultDisplay
            arView.setCameraRotation(display.rotation)
        }
    }

    companion object {
        private val TAG = ViroReleaseTestActivity::class.java.simpleName
    }
}