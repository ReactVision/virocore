//
//  VROARSceneUnderstandingiOS.mm
//  ViroKit
//
//  Created for ARCore Scene Semantics support
//  Copyright © 2024 Viro Media. All rights reserved.
//

#include "VROARSceneUnderstandingiOS.h"

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 134000

VROARSceneUnderstandingiOS::VROARSceneUnderstandingiOS() :
    _session(nullptr),
    _device(nullptr),
    _currentFrame(nullptr),
    _semanticModeEnabled(false),
    _confidenceEnabled(false),
    _semanticImage(nullptr),
    _confidenceImage(nullptr),
    _imageWidth(0),
    _imageHeight(0),
    _semanticTexture(nullptr),
    _confidenceTexture(nullptr) {
}

VROARSceneUnderstandingiOS::~VROARSceneUnderstandingiOS() {
    _session = nullptr;
    _device = nullptr;
    _currentFrame = nullptr;
    _semanticTexture = nullptr;
    _confidenceTexture = nullptr;
}

bool VROARSceneUnderstandingiOS::initialize(GARSession* session, id<MTLDevice> device) {
    _session = session;
    _device = device;
    
    // Configure ARCore session for scene semantics
    GARSessionConfiguration* config = [[GARSessionConfiguration alloc] init];
    config.semanticMode = GARSemanticModeEnabled;
    
    NSError* error;
    if (![_session setConfiguration:config error:&error]) {
        NSLog(@"Failed to configure ARCore for scene semantics: %@", error.localizedDescription);
        return false;
    }
    
    createMetalTextures();
    return true;
}

void VROARSceneUnderstandingiOS::updateFromARCoreFrame(GARFrame* frame) {
    _currentFrame = frame;
    updateSemanticData(frame);
}

const uint8_t* VROARSceneUnderstandingiOS::getSemanticImage() const {
    return _semanticImage;
}

std::vector<GARSemanticLabel> VROARSceneUnderstandingiOS::getSemanticLabelsAtPoint(float x, float y) const {
    std::vector<GARSemanticLabel> labels;
    
    if (!_currentFrame || !_semanticImage) {
        return labels;
    }
    
    // Convert screen coordinates to image coordinates
    int imageX = (int)(x * _imageWidth);
    int imageY = (int)(y * _imageHeight);
    
    if (imageX >= 0 && imageX < _imageWidth && imageY >= 0 && imageY < _imageHeight) {
        int index = imageY * _imageWidth + imageX;
        uint8_t labelValue = _semanticImage[index];
        
        if (labelValue > 0) {
            GARSemanticLabel label = (GARSemanticLabel)labelValue;
            labels.push_back(label);
        }
    }
    
    return labels;
}

bool VROARSceneUnderstandingiOS::hasSemanticLabelAtPoint(GARSemanticLabel label, float x, float y, float confidence) const {
    std::vector<GARSemanticLabel> labels = getSemanticLabelsAtPoint(x, y);
    
    for (GARSemanticLabel detectedLabel : labels) {
        if (detectedLabel == label) {
            float conf = getConfidenceAtPoint(x, y);
            return conf >= confidence;
        }
    }
    
    return false;
}

const uint8_t* VROARSceneUnderstandingiOS::getConfidenceImage() const {
    return _confidenceImage;
}

float VROARSceneUnderstandingiOS::getConfidenceAtPoint(float x, float y) const {
    if (!_confidenceImage) {
        return 0.0f;
    }
    
    int imageX = (int)(x * _imageWidth);
    int imageY = (int)(y * _imageHeight);
    
    if (imageX >= 0 && imageX < _imageWidth && imageY >= 0 && imageY < _imageHeight) {
        int index = imageY * _imageWidth + imageX;
        return _confidenceImage[index] / 255.0f;
    }
    
    return 0.0f;
}

std::vector<GARHitResult*> VROARSceneUnderstandingiOS::hitTest(float screenX, float screenY, GARSemanticLabel targetLabel) const {
    std::vector<GARHitResult*> results;
    
    if (!_session || !_currentFrame) {
        return results;
    }
    
    // Perform ARCore hit test
    NSArray<GARHitResult*>* hitResults = [_session hitTest:_currentFrame 
                                                    screenX:screenX 
                                                    screenY:screenY];
    
    // Filter by semantic label
    for (GARHitResult* hit in hitResults) {
        // Check if hit point has the target semantic label
        if (hasSemanticLabelAtPoint(targetLabel, screenX, screenY, 0.5f)) {
            results.push_back(hit);
        }
    }
    
    return results;
}

void VROARSceneUnderstandingiOS::setSemanticModeEnabled(bool enabled) {
    _semanticModeEnabled = enabled;
    
    // Update ARCore session configuration
    if (_session) {
        GARSessionConfiguration* config = [[GARSessionConfiguration alloc] init];
        config.semanticMode = enabled ? GARSemanticModeEnabled : GARSemanticModeDisabled;
        
        NSError* error;
        [_session setConfiguration:config error:&error];
    }
}

void VROARSceneUnderstandingiOS::setConfidenceEnabled(bool enabled) {
    _confidenceEnabled = enabled;
}

void VROARSceneUnderstandingiOS::updateSemanticData(GARFrame* frame) {
    if (!frame) {
        return;
    }
    
    // Get semantic image from ARCore frame
    GARSemanticImage* semanticImage = [frame acquireSemanticImage];
    if (semanticImage) {
        _semanticImage = [semanticImage pixels];
        _imageWidth = [semanticImage width];
        _imageHeight = [semanticImage height];
    }
    
    // Get confidence image if enabled
    if (_confidenceEnabled) {
        GARConfidenceImage* confidenceImage = [frame acquireConfidenceImage];
        if (confidenceImage) {
            _confidenceImage = [confidenceImage pixels];
        }
    }
}

void VROARSceneUnderstandingiOS::createMetalTextures() {
    // Create Metal textures for rendering semantic and confidence data
    // Implementation would create MTLTexture objects for GPU rendering
}

VROARSemanticLabel VROARSceneUnderstandingiOS::convertARCoreSemanticLabel(GARSemanticLabel label) {
    // Convert ARCore semantic labels to VRO semantic labels
    switch (label) {
        case GARSemanticLabelPerson: return VROARSemanticLabel::PERSON;
        case GARSemanticLabelSky: return VROARSemanticLabel::SKY;
        case GARSemanticLabelBuilding: return VROARSemanticLabel::BUILDING;
        case GARSemanticLabelTree: return VROARSemanticLabel::TREE;
        case GARSemanticLabelRoad: return VROARSemanticLabel::ROAD;
        case GARSemanticLabelCar: return VROARSemanticLabel::CAR;
        default: return VROARSemanticLabel::UNLABELED;
    }
}

GARSemanticLabel VROARSceneUnderstandingiOS::convertVROSemanticLabel(VROARSemanticLabel label) {
    // Convert VRO semantic labels to ARCore semantic labels
    switch (label) {
        case VROARSemanticLabel::PERSON: return GARSemanticLabelPerson;
        case VROARSemanticLabel::SKY: return GARSemanticLabelSky;
        case VROARSemanticLabel::BUILDING: return GARSemanticLabelBuilding;
        case VROARSemanticLabel::TREE: return GARSemanticLabelTree;
        case VROARSemanticLabel::ROAD: return GARSemanticLabelRoad;
        case VROARSemanticLabel::CAR: return GARSemanticLabelCar;
        default: return GARSemanticLabelUnlabeled;
    }
}

#endif // iOS 13.4+