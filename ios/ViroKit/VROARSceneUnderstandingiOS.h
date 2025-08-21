//
//  VROARSceneUnderstandingiOS.h
//  ViroKit
//
//  Created for ARKit Scene Understanding support (iOS 13.4+)
//  Copyright © 2024 Viro Media. All rights reserved.
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

#ifndef VROARSceneUnderstandingiOS_h
#define VROARSceneUnderstandingiOS_h

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 134000
#include "VROARSceneSemantics.h"
#import <ARCore/ARCore.h>
#include <Metal/Metal.h>
#include <memory>
#include <vector>

/*
 iOS implementation of scene understanding using ARCore's Scene Semantics API
 for cross-platform semantic understanding.
 */
class API_AVAILABLE(ios(13.4)) VROARSceneUnderstandingiOS : public VROARSceneSemantics {
public:
    
    VROARSceneUnderstandingiOS();
    virtual ~VROARSceneUnderstandingiOS();
    
    /*
     Initialize with ARCore session and Metal device.
     */
    bool initialize(GARSession* session, id<MTLDevice> device);
    
    /*
     Update scene understanding from the current ARCore frame.
     */
    void updateFromARCoreFrame(GARFrame* frame);
    
    /*
     Get semantic image from ARCore scene semantics.
     */
    const uint8_t* getSemanticImage() const;
    
    /*
     Get semantic labels for detected objects.
     */
    std::vector<GARSemanticLabel> getSemanticLabelsAtPoint(float x, float y) const;
    
    /*
     Check if a specific semantic label is present at coordinates.
     */
    bool hasSemanticLabelAtPoint(GARSemanticLabel label, float x, float y, float confidence = 0.5f) const;
    
    /*
     Get confidence map from ARCore.
     */
    const uint8_t* getConfidenceImage() const;
    
    /*
     Get semantic confidence at a specific point.
     */
    float getConfidenceAtPoint(float x, float y) const;
    
    /*
     Perform hit test against detected semantic objects.
     */
    std::vector<GARHitResult*> hitTest(float screenX, float screenY, GARSemanticLabel targetLabel) const;
    
    /*
     Enable/disable different scene understanding features.
     */
    void setSemanticModeEnabled(bool enabled);
    void setConfidenceEnabled(bool enabled);
    
    /*
     Get occlusion materials for realistic rendering.
     */
    id<MTLTexture> getOcclusionDepthTexture() const API_AVAILABLE(ios(13.4));
    id<MTLTexture> getOcclusionStencilTexture() const API_AVAILABLE(ios(13.4));
    
    /*
     Person segmentation (iOS 13.0+).
     */
    CVPixelBufferRef getSegmentationBuffer() const API_AVAILABLE(ios(13.0));
    CVPixelBufferRef getEstimatedDepthData() const API_AVAILABLE(ios(13.0));
    
    /*
     Check if person is detected at a specific point.
     */
    bool isPersonAtPoint(CGPoint point) const API_AVAILABLE(ios(13.0));
    
    /*
     Motion capture for body tracking (iOS 13.0+).
     */
    ARBody2D* getDetectedBody() const API_AVAILABLE(ios(13.0));
    ARBodyAnchor* getBodyAnchor() const API_AVAILABLE(ios(13.0));
    
    /*
     Get skeleton joint positions in 2D and 3D.
     */
    std::vector<VROVector2f> getBodyJoints2D() const API_AVAILABLE(ios(13.0));
    std::vector<VROVector3f> getBodyJoints3D() const API_AVAILABLE(ios(13.0));
    
private:
    
    GARSession* _session;
    id<MTLDevice> _device;
    GARFrame* _currentFrame;
    
    // Scene semantics
    bool _semanticModeEnabled;
    bool _confidenceEnabled;
    
    // Cached semantic data
    const uint8_t* _semanticImage;
    const uint8_t* _confidenceImage;
    int _imageWidth;
    int _imageHeight;
    
    // Metal textures for rendering
    id<MTLTexture> _semanticTexture;
    id<MTLTexture> _confidenceTexture;
    
    void updateSemanticData(GARFrame* frame);
    void createMetalTextures();
    
    // Convert between ARCore and VRO semantic labels
    static VROARSemanticLabel convertARCoreSemanticLabel(GARSemanticLabel label);
    static GARSemanticLabel convertVROSemanticLabel(VROARSemanticLabel label);
};

#endif // iOS 13.4+
#endif /* VROARSceneUnderstandingiOS_h */