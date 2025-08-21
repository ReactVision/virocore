//
//  VROARSceneSemantics.h
//  ViroRenderer
//
//  Created for Scene Semantics API support
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

#ifndef VROARSceneSemantics_h
#define VROARSceneSemantics_h

#include <memory>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include "VROVector3f.h"
#include "VROMatrix4f.h"
#include "VROTexture.h"

enum class VROARSemanticMode {
    Disabled,
    Enabled
};

enum class VROARSemanticLabel {
    Unknown = 0,
    Sky = 1,
    Building = 2,
    Tree = 3,
    Road = 4,
    Sidewalk = 5,
    Terrain = 6,
    Structure = 7,
    Object = 8,
    Water = 9,
    Person = 10,
    Ground = 11,
    Ceiling = 12,
    Wall = 13,
    Floor = 14,
    Furniture = 15,
    Door = 16,
    Window = 17,
    Plant = 18,
    Table = 19,
    Chair = 20,
    Car = 21,
    Bike = 22
};

/*
 Provides semantic understanding of the scene through pixel-wise labeling
 and confidence maps. Available in ARCore 1.31+ and ARKit 14+.
 */
class VROARSceneSemantics {
    
public:
    
    VROARSceneSemantics() : 
        _semanticsEnabled(false),
        _id("semantics_" + std::to_string(reinterpret_cast<uintptr_t>(this))),
        _imageWidth(0),
        _imageHeight(0) {}
        
    virtual ~VROARSceneSemantics() {}
    
    /*
     Enable or disable scene semantics processing.
     */
    bool isEnabled() const { return _semanticsEnabled; }
    void setEnabled(bool enabled) { _semanticsEnabled = enabled; }
    
    /*
     Get the semantic label at a given pixel coordinate.
     Returns Unknown if the coordinate is out of bounds or semantics unavailable.
     */
    VROARSemanticLabel getLabelAtPixel(int x, int y) const;
    
    /*
     Get the confidence value [0.0, 1.0] for a specific label at a pixel.
     */
    float getConfidenceForLabelAtPixel(VROARSemanticLabel label, int x, int y) const;
    
    /*
     Get all pixels that match a specific semantic label with confidence above threshold.
     */
    std::vector<VROVector3f> getPixelsForLabel(VROARSemanticLabel label, float minConfidence = 0.5f) const;
    
    /*
     Get the semantic label mask as a texture for visualization.
     Each pixel contains the label ID as a color value.
     */
    std::shared_ptr<VROTexture> getSemanticLabelTexture() const { return _labelTexture; }
    
    /*
     Get the confidence map for a specific label as a grayscale texture.
     */
    std::shared_ptr<VROTexture> getConfidenceTexture(VROARSemanticLabel label) const;
    
    /*
     Query if a specific semantic label is detected in the current frame.
     */
    bool hasLabel(VROARSemanticLabel label, float minConfidence = 0.5f) const;
    
    /*
     Get the percentage of pixels that match a specific label.
     */
    float getLabelCoverage(VROARSemanticLabel label, float minConfidence = 0.5f) const;
    
    /*
     Update the semantic data from the current AR frame.
     Called internally by the AR session.
     */
    void updateFromFrame(const uint8_t* labelData, const float* confidenceData,
                        int width, int height);
    
    /*
     Get the dimensions of the semantic image.
     */
    int getImageWidth() const { return _imageWidth; }
    int getImageHeight() const { return _imageHeight; }
    
    /*
     Get the unique identifier for this semantics object.
     */
    std::string getId() const { return _id; }
    
    /*
     Convert between semantic image coordinates and viewport coordinates.
     */
    VROVector3f semanticToViewport(const VROVector3f& semanticCoord) const;
    VROVector3f viewportToSemantic(const VROVector3f& viewportCoord) const;
    
private:
    
    bool _semanticsEnabled;
    int _imageWidth;
    int _imageHeight;
    std::string _id;
    
    // Raw semantic data
    std::vector<uint8_t> _labelData;
    std::vector<float> _confidenceData;
    
    // Cached textures for visualization
    std::shared_ptr<VROTexture> _labelTexture;
    std::map<VROARSemanticLabel, std::shared_ptr<VROTexture>> _confidenceTextures;
    
    // Transform between coordinate spaces
    VROMatrix4f _semanticToViewportTransform;
    VROMatrix4f _viewportToSemanticTransform;
    
    void updateTextures();
    int getPixelIndex(int x, int y) const;
    VROVector3f getLabelColor(VROARSemanticLabel label) const;
};

#endif /* VROARSceneSemantics_h */