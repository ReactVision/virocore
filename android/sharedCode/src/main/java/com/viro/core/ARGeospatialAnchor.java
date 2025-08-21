//
//  Copyright (c) 2024-present, ViroMedia, Inc.
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

/**
 * ARGeospatialAnchor represents an anchor placed at real-world geographic coordinates
 * using the ARCore Geospatial API. This enables AR experiences that are precisely
 * aligned with the physical world using GPS coordinates and visual positioning.
 * 
 * Available in ARCore 1.31+ with Geospatial API enabled.
 */
public class ARGeospatialAnchor extends ARAnchor {
    
    /**
     * Geospatial modes for AR session configuration.
     */
    public enum GeospatialMode {
        /**
         * Geospatial API is disabled.
         */
        DISABLED,
        
        /**
         * Geospatial API is enabled with WGS84 altitude.
         */
        ENABLED,
        
        /**
         * Geospatial API is enabled with terrain and rooftop anchors support.
         * Requires ARCore 1.31+ for terrain, 1.38+ for rooftop.
         */
        ENABLED_WITH_TERRAIN_ANCHORS
    }
    
    /**
     * Altitude modes for geospatial anchors.
     */
    public enum AltitudeMode {
        /**
         * Altitude is relative to WGS84 ellipsoid.
         */
        WGS84,
        
        /**
         * Altitude is relative to terrain at the anchor location.
         */
        TERRAIN,
        
        /**
         * Altitude is relative to building rooftop at the anchor location.
         */
        ROOFTOP
    }
    
    /**
     * State of rooftop anchor resolution.
     */
    public enum RooftopAnchorState {
        NONE,
        SUCCESS,
        ERROR_INTERNAL,
        ERROR_NOT_AUTHORIZED,
        ERROR_UNSUPPORTED_LOCATION
    }
    
    /**
     * VPS (Visual Positioning Service) availability status.
     */
    public enum VPSAvailability {
        UNKNOWN,
        AVAILABLE,
        UNAVAILABLE,
        ERROR_NETWORK_CONNECTION,
        ERROR_INTERNAL
    }
    
    /**
     * Callback for async terrain/rooftop anchor creation.
     */
    public interface AnchorResolveCallback {
        void onResolve(ARGeospatialAnchor anchor, double altitude);
        void onError(String error);
    }
    
    /**
     * Callback for VPS availability check.
     */
    public interface VPSAvailabilityCallback {
        void onResult(VPSAvailability availability);
    }
    
    private double mLatitude;
    private double mLongitude;
    private double mAltitude;
    private Quaternion mEastUpSouthOrientation;
    private AltitudeMode mAltitudeMode;
    private RooftopAnchorState mRooftopState;
    
    ARGeospatialAnchor(String anchorId, ARScene session) {
        super(anchorId, "", "GEOSPATIAL", 
              new float[]{0, 0, 0}, new float[]{0, 0, 0}, new float[]{1, 1, 1});
        mAltitudeMode = AltitudeMode.WGS84;
        mRooftopState = RooftopAnchorState.NONE;
    }
    
    /**
     * Create a geospatial anchor at the specified coordinates.
     * 
     * @param latitude The latitude in degrees
     * @param longitude The longitude in degrees  
     * @param altitude The altitude in meters (WGS84)
     * @param orientation The orientation in East-Up-South coordinate system
     * @return The created geospatial anchor
     */
    public static ARGeospatialAnchor create(ARScene session,
                                           double latitude,
                                           double longitude,
                                           double altitude,
                                           Quaternion orientation) {
        // TODO: Implement geospatial anchor creation
        // For now, return null until JNI implementation is complete
        return null;
    }
    
    /**
     * Create a terrain anchor with altitude relative to terrain.
     * The altitude will be resolved asynchronously.
     * 
     * @param latitude The latitude in degrees
     * @param longitude The longitude in degrees
     * @param orientation The orientation in East-Up-South coordinate system
     * @param callback Called when terrain altitude is resolved
     * @return The created geospatial anchor
     */
    public static ARGeospatialAnchor createTerrainAnchor(ARScene session,
                                                        double latitude,
                                                        double longitude,
                                                        Quaternion orientation,
                                                        AnchorResolveCallback callback) {
        // TODO: Implement terrain anchor creation
        // For now, return null until JNI implementation is complete
        if (callback != null) {
            callback.onError("Terrain anchors not yet implemented");
        }
        return null;
    }
    
    /**
     * Create a rooftop anchor with altitude relative to building rooftop.
     * The altitude will be resolved asynchronously.
     * 
     * @param latitude The latitude in degrees
     * @param longitude The longitude in degrees
     * @param orientation The orientation in East-Up-South coordinate system
     * @param callback Called when rooftop altitude is resolved
     * @return The created geospatial anchor
     */
    public static ARGeospatialAnchor createRooftopAnchor(ARScene session,
                                                        double latitude,
                                                        double longitude,
                                                        Quaternion orientation,
                                                        AnchorResolveCallback callback) {
        // TODO: Implement rooftop anchor creation
        // For now, return null until JNI implementation is complete
        if (callback != null) {
            callback.onError("Rooftop anchors not yet implemented");
        }
        return null;
    }
    
    /**
     * Get the latitude of this anchor in degrees.
     */
    public double getLatitude() {
        return mLatitude;
    }
    
    /**
     * Get the longitude of this anchor in degrees.
     */
    public double getLongitude() {
        return mLongitude;
    }
    
    /**
     * Get the altitude of this anchor in meters.
     * The reference (WGS84, terrain, or rooftop) depends on the altitude mode.
     */
    public double getAltitude() {
        return mAltitude;
    }
    
    /**
     * Get the orientation of this anchor in East-Up-South coordinate system.
     */
    public Quaternion getEastUpSouthOrientation() {
        return mEastUpSouthOrientation;
    }
    
    /**
     * Get the altitude mode of this anchor.
     */
    public AltitudeMode getAltitudeMode() {
        return mAltitudeMode;
    }
    
    /**
     * Get the rooftop anchor state (for rooftop anchors only).
     */
    public RooftopAnchorState getRooftopState() {
        return mRooftopState;
    }
    
    /**
     * Check VPS availability at a specific location.
     * 
     * @param session The AR session
     * @param latitude The latitude to check
     * @param longitude The longitude to check
     * @param callback Called with availability status
     */
    public static void checkVPSAvailability(ARScene session,
                                           double latitude,
                                           double longitude,
                                           VPSAvailabilityCallback callback) {
        // TODO: Implement VPS availability checking
        // For now, call callback with unavailable status
        if (callback != null) {
            callback.onResult(VPSAvailability.UNAVAILABLE);
        }
    }
    
    // Callback wrappers for JNI
    private static void registerTerrainCallback(long anchorRef,
                                               ARGeospatialAnchor anchor,
                                               AnchorResolveCallback callback) {
        // TODO: Implement terrain callback registration
        // For now, do nothing
    }
    
    private static void registerRooftopCallback(long anchorRef,
                                               ARGeospatialAnchor anchor,
                                               AnchorResolveCallback callback) {
        // TODO: Implement rooftop callback registration
        // For now, do nothing
    }
    
    private static class TerrainCallbackWrapper {
        private ARGeospatialAnchor mAnchor;
        private AnchorResolveCallback mCallback;
        
        TerrainCallbackWrapper(ARGeospatialAnchor anchor, AnchorResolveCallback callback) {
            mAnchor = anchor;
            mCallback = callback;
        }
        
        // Called from native
        void onResolve(double altitude) {
            mAnchor.mAltitude = altitude;
            mCallback.onResolve(mAnchor, altitude);
        }
        
        void onError(String error) {
            mCallback.onError(error);
        }
    }
    
    private static class RooftopCallbackWrapper {
        private ARGeospatialAnchor mAnchor;
        private AnchorResolveCallback mCallback;
        
        RooftopCallbackWrapper(ARGeospatialAnchor anchor, AnchorResolveCallback callback) {
            mAnchor = anchor;
            mCallback = callback;
        }
        
        // Called from native
        void onResolve(double altitude, int rooftopState) {
            mAnchor.mAltitude = altitude;
            mAnchor.mRooftopState = RooftopAnchorState.values()[rooftopState];
            mCallback.onResolve(mAnchor, altitude);
        }
        
        void onError(String error) {
            mCallback.onError(error);
        }
    }
    
    private static class VPSAvailabilityCallbackWrapper {
        private VPSAvailabilityCallback mCallback;
        
        VPSAvailabilityCallbackWrapper(VPSAvailabilityCallback callback) {
            mCallback = callback;
        }
        
        // Called from native
        void onResult(int availability) {
            mCallback.onResult(VPSAvailability.values()[availability]);
        }
    }
    
    // Native methods
    // TODO: Native method implementations will be added when JNI layer is complete
}