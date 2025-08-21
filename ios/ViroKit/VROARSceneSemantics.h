//
//  VROARSceneSemantics.h
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#ifndef VROARSceneSemantics_h
#define VROARSceneSemantics_h

#include "VROVector3f.h"
#include <vector>
#include <string>
#include <functional>

enum class VROARSemanticLabel {
    UNLABELED = 0,
    SKY,
    BUILDING,
    TREE,
    ROAD,
    SIDEWALK,
    TERRAIN,
    STRUCTURE,
    OBJECT,
    VEHICLE,
    PERSON,
    WATER,
    VEGETATION,
    CAR,
    TRUCK,
    BUS,
    MOTORCYCLE,
    BICYCLE,
    TRAFFIC_LIGHT,
    TRAFFIC_SIGN,
    FENCE
};

class VROARSceneSemantics {
public:
    VROARSceneSemantics();
    ~VROARSceneSemantics();
    
    // Enable/disable semantic segmentation
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }
    
    // Get semantic labels at screen point
    std::vector<VROARSemanticLabel> getLabelsAtPoint(float screenX, float screenY) const;
    
    // Check if a specific label exists at screen point
    bool hasLabel(VROARSemanticLabel label, float screenX, float screenY, float minConfidence = 0.5f) const;
    
    // Get all pixels for a specific label
    std::vector<VROVector3f> getPixelsForLabel(VROARSemanticLabel label, float minConfidence = 0.5f) const;
    
    // Get confidence for a label at specific point
    float getConfidence(VROARSemanticLabel label, float screenX, float screenY) const;
    
    // Update semantic data from ARCore
    void updateFromARCore(void* garFrame);
    
    // Utility methods
    std::string getLabelName(VROARSemanticLabel label) const;
    std::string getId() const { return _id; }
    
private:
    bool _enabled;
    std::string _id;
    void* _garSemantics; // Reference to ARCore semantics data
    
    // Cached semantic data
    std::vector<uint8_t> _semanticImage;
    std::vector<float> _confidenceImage;
    int _imageWidth;
    int _imageHeight;
    
    void updateSemanticData();
};

#endif /* VROARSceneSemantics_h */