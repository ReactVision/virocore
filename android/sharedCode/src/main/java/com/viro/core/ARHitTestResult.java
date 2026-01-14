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

package com.viro.core;

import android.util.Log;

import java.util.HashMap;
import java.util.Map;

/**
 * ARHitTestResult encapsulates a single result of an AR hit-test. AR hit-tests are initiated from
 * the {@link ViroViewARCore}. These hit tests are a mechanism for you to
 * discover what real-world objects are contained along a given ray in the {@link
 * Scene} or at a given 2D point on the {@link
 * ViroViewARCore}.
 */
public class ARHitTestResult {

    /**
     * Hit-tests can intersect different kinds of real-world features, each identified by their
     * Type.
     */
    public enum Type {
        /**
         * Indicates a feature-point was found by the hit-test. Feature points are points that have
         * been identified as part of a continuous surface, but have not yet been correlated into
         * a plane.
         */
        FEATURE_POINT("FeaturePoint"),
        /**
         * Indicates a plane was found by the hit-test.
         */
        PLANE("ExistingPlaneUsingExtent"),
        /**
         * Indicates a depth point was found by the hit-test using depth data.
         * Requires depth mode to be enabled in ARCore configuration.
         * The orientation is perpendicular to the 3D surface at the hit location.
         */
        DEPTH_POINT("DepthPoint");

        private final String mStringValue;

        public static Type forString(String string) {
            for (Type format : Type.values()) {
                if (format.getStringValue().equalsIgnoreCase(string)) {
                    return format;
                }
            }
            throw new IllegalArgumentException("Invalid ARHitTestResult.Type [" + string + "]");
        }

        private Type(String value) {
            this.mStringValue = value;
        }
        /**
         * @hide
         */
        public String getStringValue() {
            return this.mStringValue;
        }

        private static Map<String, Type> map = new HashMap<String, Type>();
        static {
            for (Type value : Type.values()) {
                map.put(value.getStringValue().toLowerCase(), value);
            }
        }
        /**
         * @hide
         */
        public static Type valueFromString(String str) {
            return str == null ? null : map.get(str.toLowerCase());
        }
    }

    private long mNativeRef;
    private Type mType;
    private Vector mPosition;
    private Vector mScale;
    private Vector mRotation;
    private ARNode mARNode;

    // Depth-related fields
    private boolean mHasDepthData;
    private float mDepthValue;
    private float mDepthConfidence;
    private String mDepthSource;

    /**
     * Invoked by Native.
     * @hide
     */
    ARHitTestResult(long nativeRef, String type, float[] position, float[] scale, float[] rotation,
                    boolean hasDepthData, float depthValue, float depthConfidence, String depthSource) {
        mNativeRef = nativeRef;
        mType = Type.valueFromString(type);
        mPosition = new Vector(position);
        mScale = new Vector(scale);
        mRotation = new Vector(rotation);
        mHasDepthData = hasDepthData;
        mDepthValue = depthValue;
        mDepthConfidence = depthConfidence;
        mDepthSource = depthSource;
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            dispose();
        } finally {
            super.finalize();
        }
    }

    /**
     * Release native resources associated with this ARHitTestResult.
     */
    public void dispose() {
        if (mNativeRef != 0) {
            nativeDestroyARHitTestResult(mNativeRef);
            mNativeRef = 0;
        }
    }

    /**
     * Returns the kind of detected real-world object contained in this result.
     *
     * @return The type of the real-world object.
     */
    public Type getType() {
        return mType;
    }

    /**
     * Returns the position of the detected real-world object, in world coordinates.
     *
     * @return The position in world coordinates, in a {@link Vector}.
     */
    public Vector getPosition() {
        return mPosition;
    }

    /**
     * Return the scale of the detected real-world object.
     *
     * @return The scale in a {@link Vector}.
     */
    public Vector getScale() {
        return mScale;
    }

    /**
     * Return the orientation of the detected real-world object, along each of the three
     * principal axes.
     *
     * @return The rotation in radians along the X, Y, and Z axes, stored in a {@link Vector}.
     */
    public Vector getRotation() {
        return mRotation;
    }

    /**
     * Returns true if depth data is available for this hit test result.
     *
     * @return True if depth data is available, false otherwise.
     */
    public boolean hasDepthData() {
        return mHasDepthData;
    }

    /**
     * Get the depth value at the hit point in meters.
     * Only valid if hasDepthData() returns true.
     *
     * @return Depth in meters from the camera to the hit point.
     */
    public float getDepthValue() {
        return mDepthValue;
    }

    /**
     * Get the depth confidence value (0-1).
     * Returns -1 if confidence data is not available.
     * Only valid if hasDepthData() returns true.
     *
     * @return Confidence value between 0 and 1, or -1 if not available.
     */
    public float getDepthConfidence() {
        return mDepthConfidence;
    }

    /**
     * Get the source of depth data: "arcore", "lidar", "monocular", or "none".
     *
     * @return String indicating the depth data source.
     */
    public String getDepthSource() {
        return mDepthSource;
    }

    /**
     * Create an {@link ARNode} that will be anchored to the real-world position associated with
     * this hit result. Anchoring a node to a hit-result will help ensure that the objects you
     * attach to this node will track properly and remain in place.
     * <p>
     * The returned {@link ARNode} is automatically added to the Scene, and will be
     * continually updated to stay in the sync with its underlying anchor as the anchor's
     * properties, orientation, or position change.
     * <p>
     * If there is already an ARNode associated with this hit result, that ARNode will be returned
     * here. <b>If AR tracking is limited, this method will return null.</b>
     * <p>
     * When finished with this ARNode, you must call {@link ARNode#detach()} to remove it from
     * the system. If you do not detach the ARNode, it will continue to receive tracking updates
     * from the AR subsystem, adversely impacting performance.
     * <p>
     *
     * @return New {@link ARNode} anchored to the hit result position, or null if AR tracking is
     * currently limited.
     */
    public ARNode createAnchoredNode() {
        if (mARNode == null) {
            long nodeRef = nativeCreateAnchoredNode(mNativeRef);
            if (nodeRef == 0) {
                return null;
            } else {
                mARNode = new ARNode(nativeCreateAnchoredNode(mNativeRef));
            }
        }
        return mARNode;
    }

    private native long nativeCreateAnchoredNode(long hitResultRef);
    private native void nativeDestroyARHitTestResult(long hitResultRef);

}
