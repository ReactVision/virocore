//
//  VROARFrameARCore.h
//  ViroKit
//
//  Copyright © 2018 Viro Media. All rights reserved.
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

#ifndef VROARFrameARCore_h
#define VROARFrameARCore_h

#include "VROCameraTexture.h"
#include "VROARFrame.h"
#include "VROViewport.h"
#include "ARCore_API.h"

class VROARSessionARCore;
class VROTexture;
class VRODriver;

class VROARFrameARCore : public VROARFrame {
public:
    
    VROARFrameARCore(arcore::Frame *frame, VROViewport viewport, std::shared_ptr<VROARSessionARCore> session);
    virtual ~VROARFrameARCore();
    
    double getTimestamp() const;
    
    const std::shared_ptr<VROARCamera> &getCamera() const;
    VROCameraOrientation getOrientation() const {
        return VROCameraOrientation::Portrait;
    }
    std::vector<std::shared_ptr<VROARHitTestResult>> hitTest(int x, int y, std::set<VROARHitTestResultType> types);
    std::vector<std::shared_ptr<VROARHitTestResult>> hitTestRay(VROVector3f *origin, VROVector3f *destination , std::set<VROARHitTestResultType> types);
    VROMatrix4f getViewportToCameraImageTransform() const;
    const std::vector<std::shared_ptr<VROARAnchor>> &getAnchors() const;
    
    float getAmbientLightIntensity() const;
    VROVector3f getAmbientLightColor() const;

    bool hasDisplayGeometryChanged();
    void getBackgroundTexcoords(VROVector3f *BL, VROVector3f *BR, VROVector3f *TL, VROVector3f *TR);

    arcore::Frame *getFrameInternal() {
        return _frame;
    }

    std::shared_ptr<VROARPointCloud> getPointCloud();

    /*
     * Depth texture methods for AR occlusion support.
     */
    std::shared_ptr<VROTexture> getDepthTexture() override;
    std::shared_ptr<VROTexture> getDepthConfidenceTexture() override;
    bool hasDepthData() const override;
    int getDepthImageWidth() const override;
    int getDepthImageHeight() const override;

    /*
     * Scene Semantics methods.
     */
    bool hasSemanticData() const override;
    VROSemanticImage getSemanticImage() override;
    VROSemanticConfidenceImage getSemanticConfidenceImage() override;
    float getSemanticLabelFraction(VROSemanticLabel label) override;
    int getSemanticImageWidth() const override;
    int getSemanticImageHeight() const override;

    /*
     * Set the driver needed for texture creation.
     */
    void setDriver(std::shared_ptr<VRODriver> driver) {
        _driver = driver;
    }

private:

    arcore::Frame *_frame;
    std::weak_ptr<VROARSessionARCore> _session;
    std::shared_ptr<VROARCamera> _camera;
    VROViewport _viewport;
    std::vector<std::shared_ptr<VROARAnchor>> _anchors; // Unused in ARCore
    std::shared_ptr<VROARPointCloud> _pointCloud;

    // Driver for creating textures
    std::weak_ptr<VRODriver> _driver;

    // Cached depth texture (refreshed each frame)
    std::shared_ptr<VROTexture> _depthTexture;
    std::shared_ptr<VROTexture> _depthConfidenceTexture;
    int _depthWidth = 0;
    int _depthHeight = 0;
    bool _depthDataAvailable = false;

    // Cached semantic data (refreshed each frame)
    mutable VROSemanticImage _semanticImage;
    mutable VROSemanticConfidenceImage _semanticConfidenceImage;
    mutable int _semanticWidth = 0;
    mutable int _semanticHeight = 0;
    mutable bool _semanticDataAvailable = false;
    mutable bool _semanticDataChecked = false;

    /*
     * Internal method to acquire and convert depth data to texture.
     */
    void acquireDepthData();

    /*
     * Internal method to acquire semantic data from ARCore.
     */
    void acquireSemanticData() const;

};

#endif /* VROARFrameARCore_h */
