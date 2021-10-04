/*
 * Copyright (c) 2017-present, Viro, Inc.
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.example.virosample.placingObjects

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.app.Dialog
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.coordinatorlayout.widget.CoordinatorLayout
import com.google.android.material.snackbar.Snackbar
import com.viro.core.*
import com.viro.core.ARScene.TrackingStateReason
import com.viro.core.Vector
import java.lang.ref.WeakReference
import java.util.*

/**
 * Activity that initializes Viro and ARCore. This activity builds an AR scene that lets the user
 * place and drag objects. Tap on the 'Viro' button to get a dialog of objects to place in the scene.
 * Once placed, the objects can be dragged, rotated, and scaled using pinch and rotate gestures.
 */
class ViroActivity : Activity() {
    private lateinit var viroView: ViroView

    /**
     * The ARScene we will be creating within this activity.
     */
    private var mScene: ARScene? = null

    private var draggableObjects: MutableList<Draggable3DObject>? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        draggableObjects = ArrayList()
        viroView = ViroViewARCore(this, object : ViroViewARCore.StartupListener {
            override fun onSuccess() {
                displayScene()
            }

            override fun onFailure(error: ViroViewARCore.StartupError, errorMessage: String) {
                Log.e(TAG, "Error initializing [$errorMessage]")
                showSnackbar("Error initializing [$errorMessage]")
            }
        })
        setContentView(viroView)
    }

    /**
     * Contains logic for placing, dragging, rotating, and scaling a 3D object in AR.
     */
    private inner class Draggable3DObject(private val mFileName: String) {
        private var rotateStart = 0f
        private var scaleStart = 0f
        fun addModelToPosition(position: Vector) {
            val object3D = Object3D()
            object3D.setPosition(position)
            // Shrink the objects as the original size is too large.
            object3D.setScale(Vector(.2f, .2f, .2f))
            object3D.gestureRotateListener = GestureRotateListener { _, _, rotation, rotateState ->
                if (rotateState == RotateState.ROTATE_START) {
                    rotateStart = object3D.rotationEulerRealtime.y
                }
                val totalRotationY = rotateStart + rotation
                object3D.setRotation(Vector(0.0, totalRotationY.toDouble(), 0.0))
            }
            object3D.gesturePinchListener = GesturePinchListener { _, _, scale, pinchState ->
                if (pinchState == PinchState.PINCH_START) {
                    scaleStart = object3D.scaleRealtime.x
                } else {
                    object3D.setScale(Vector(scaleStart * scale, scaleStart * scale, scaleStart * scale))
                }
            }
            object3D.dragListener = DragListener { _, _, _, _ -> }

            // Load the Android model asynchronously.
            object3D.loadModel(viroView.viroContext, Uri.parse(mFileName), Object3D.Type.FBX, object : AsyncObject3DListener {
                override fun onObject3DLoaded(`object`: Object3D, type: Object3D.Type) {}
                override fun onObject3DFailed(s: String) {
                    Toast.makeText(this@ViroActivity, "An error occurred when loading the 3D Object!", Toast.LENGTH_LONG).show()
                }
            })

            // Make the object draggable.
            object3D.dragType = Node.DragType.FIXED_TO_WORLD
            mScene!!.rootNode.addChildNode(object3D)
        }
    }

    private fun displayScene() {
        mScene = ARScene()
        // Add a listener to the scene so we can update the 'AR Initialized' text.
        mScene!!.setListener(ARSceneListener(this))
        // Add a light to the scene so our models show up
        mScene!!.rootNode.addLight(AmbientLight(Color.WHITE.toLong(), 1000f))
        viroView.scene = mScene
        View.inflate(this, R.layout.viro_view_ar_hit_test_hud, viroView as ViewGroup?)

        (findViewById<View>(R.id.imageButton) as ImageButton).setOnClickListener { showPopup() }
    }

    /**
     * Perform a hit-test and place the object (identified by its file name) at the intersected
     * location.
     *
     * @param fileName The resource name of the object to place.
     */
    private fun placeObject(fileName: String) {
        val viewARView = viroView as ViroViewARCore?
        val cameraPos = viewARView!!.lastCameraPositionRealtime
        viewARView.performARHitTestWithRay(viewARView.lastCameraForwardRealtime, ARHitTestListener { arHitTestResults ->
            if (arHitTestResults != null && arHitTestResults.isNotEmpty()) {
                for (i in arHitTestResults.indices) {
                    val result = arHitTestResults[i]
                    val distance = result.position.distance(cameraPos)
                    if (distance > MIN_DISTANCE && distance < MAX_DISTANCE) {
                        // If we found a plane or feature point further than 0.2m and less 10m away,
                        // then choose it!
                        add3DDraggableObject(fileName, result.position)
                        return@ARHitTestListener
                    }
                }
            }
            Toast.makeText(
                this@ViroActivity, "Unable to find suitable point or plane to place object!",
                Toast.LENGTH_LONG
            ).show()
        })
    }

    /**
     * Add a 3D object with the given filename to the scene at the specified world position.
     */
    private fun add3DDraggableObject(filename: String, position: Vector) {
        val draggable3DObject = Draggable3DObject(filename)
        draggableObjects!!.add(draggable3DObject)
        draggable3DObject.addModelToPosition(position)
    }

    /**
     * Dialog menu displaying the virtual objects we can place in the real world.
     */
    private fun showPopup() {
        val builder = AlertDialog.Builder(this)
        val itemsList = arrayOf<CharSequence>("Coffee mug", "Flowers", "Smile Emoji")
        builder.setTitle("Choose an object")
            .setItems(itemsList) { _, which ->
                when (which) {
                    0 -> placeObject("file:///android_asset/object_coffee_mug.vrx")
                    1 -> placeObject("file:///android_asset/object_flowers.vrx")
                    2 -> placeObject("file:///android_asset/emoji_smile.vrx")
                }
            }
        val d: Dialog = builder.create()
        d.show()
    }

    /**
     * Private class that implements ARScene.Listener callbacks. In this example we use this to notify
     * the user when AR is initialized.
     */
    private class ARSceneListener(activity: Activity) : ARScene.Listener {

        private val currentActivityWeak: WeakReference<Activity> = WeakReference(activity)
        private var initialized: Boolean = false

        @SuppressLint("SetTextI18n")
        override fun onTrackingUpdated(
            trackingState: ARScene.TrackingState,
            trackingStateReason: TrackingStateReason
        ) {
            if (!initialized && trackingState == ARScene.TrackingState.NORMAL) {
                val activity = currentActivityWeak.get() ?: return
                val initText = activity.findViewById<View>(R.id.initText) as TextView
                initText.text = "AR is initialized"
                initialized = true
            }
        }

        override fun onTrackingInitialized() = Unit

        override fun onAmbientLightUpdate(lightIntensity: Float, lightColor: Vector) = Unit

        override fun onAnchorFound(arAnchor: ARAnchor, arNode: ARNode) = Unit
        override fun onAnchorRemoved(arAnchor: ARAnchor, arNode: ARNode) = Unit
        override fun onAnchorUpdated(arAnchor: ARAnchor, arNode: ARNode) = Unit

    }

    override fun onStart() {
        super.onStart()
        viroView.onActivityStarted(this)
    }

    override fun onResume() {
        super.onResume()
        viroView.onActivityResumed(this)
    }

    override fun onPause() {
        super.onPause()
        viroView.onActivityPaused(this)
    }

    override fun onStop() {
        super.onStop()
        viroView.onActivityStopped(this)
    }

    override fun onDestroy() {
        super.onDestroy()
        viroView.onActivityDestroyed(this)
    }

    fun showSnackbar(text: String, lengthShow: Int = Snackbar.LENGTH_INDEFINITE) {
        val viewPos: View? = findViewById(android.R.id.content)
        val snackBar = Snackbar.make(viewPos!!, text, lengthShow)
        val view = snackBar.view
        val params = view.layoutParams
        if (params is CoordinatorLayout.LayoutParams) {
            val paramsC = view.layoutParams as CoordinatorLayout.LayoutParams
            paramsC.gravity = Gravity.CENTER_VERTICAL
            view.layoutParams = paramsC
            snackBar.show()
        } else {
            snackBar.show()
        }
    }

    companion object {
        private val TAG = ViroActivity::class.java.simpleName

        // Constants used to determine if plane or point is within bounds. Units in meters.
        private const val MIN_DISTANCE = 0.2f
        private const val MAX_DISTANCE = 10f
    }
}
