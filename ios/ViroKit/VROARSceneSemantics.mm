//
//  VROARSceneSemantics.mm
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#import "VROARSceneSemantics.h"
#import <ARCore/ARCore.h>
#import <Foundation/Foundation.h>

VROARSceneSemantics::VROARSceneSemantics() :
    _enabled(false),
    _id([[NSUUID UUID] UUIDString].UTF8String),
    _garSemantics(nullptr),
    _imageWidth(0),
    _imageHeight(0) {
}

VROARSceneSemantics::~VROARSceneSemantics() {
    if (_garSemantics) {
        _garSemantics = nullptr;
    }
}

void VROARSceneSemantics::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
        _semanticImage.clear();
        _confidenceImage.clear();
    }
}

std::vector<VROARSemanticLabel> VROARSceneSemantics::getLabelsAtPoint(float screenX, float screenY) const {
    std::vector<VROARSemanticLabel> labels;
    
    if (!_enabled || _semanticImage.empty()) {
        return labels;
    }
    
    // Convert screen coordinates to image coordinates
    int x = (int)(screenX * _imageWidth);
    int y = (int)(screenY * _imageHeight);
    
    if (x >= 0 && x < _imageWidth && y >= 0 && y < _imageHeight) {
        int index = y * _imageWidth + x;
        if (index < _semanticImage.size()) {
            uint8_t labelValue = _semanticImage[index];
            if (labelValue < static_cast<uint8_t>(VROARSemanticLabel::FENCE) + 1) {
                labels.push_back(static_cast<VROARSemanticLabel>(labelValue));
            }
        }
    }
    
    return labels;
}

bool VROARSceneSemantics::hasLabel(VROARSemanticLabel label, float screenX, float screenY, float minConfidence) const {
    if (!_enabled) {
        return false;
    }
    
    std::vector<VROARSemanticLabel> labels = getLabelsAtPoint(screenX, screenY);
    for (VROARSemanticLabel foundLabel : labels) {
        if (foundLabel == label) {
            float confidence = getConfidence(label, screenX, screenY);
            return confidence >= minConfidence;
        }
    }
    
    return false;
}

std::vector<VROVector3f> VROARSceneSemantics::getPixelsForLabel(VROARSemanticLabel label, float minConfidence) const {
    std::vector<VROVector3f> pixels;
    
    if (!_enabled || _semanticImage.empty()) {
        return pixels;
    }
    
    uint8_t targetLabel = static_cast<uint8_t>(label);
    
    for (int y = 0; y < _imageHeight; y++) {
        for (int x = 0; x < _imageWidth; x++) {
            int index = y * _imageWidth + x;
            if (index < _semanticImage.size() && _semanticImage[index] == targetLabel) {
                if (index < _confidenceImage.size() && _confidenceImage[index] >= minConfidence) {
                    pixels.push_back(VROVector3f(x, y, _confidenceImage[index]));
                }
            }
        }
    }
    
    return pixels;
}

float VROARSceneSemantics::getConfidence(VROARSemanticLabel label, float screenX, float screenY) const {
    if (!_enabled || _confidenceImage.empty()) {
        return 0.0f;
    }
    
    int x = (int)(screenX * _imageWidth);
    int y = (int)(screenY * _imageHeight);
    
    if (x >= 0 && x < _imageWidth && y >= 0 && y < _imageHeight) {
        int index = y * _imageWidth + x;
        if (index < _confidenceImage.size()) {
            return _confidenceImage[index];
        }
    }
    
    return 0.0f;
}

void VROARSceneSemantics::updateFromARCore(void* garFrame) {
    if (!_enabled || !garFrame) {
        return;
    }
    
    _garSemantics = garFrame;
    updateSemanticData();
}

void VROARSceneSemantics::updateSemanticData() {
    if (!_garSemantics) {
        return;
    }
    
    // In real implementation, this would extract semantic data from ARCore
    // GARFrame *frame = (__bridge GARFrame *)_garSemantics;
    // Extract semantic segmentation image and confidence map from ARCore
    
    // Placeholder implementation
    _imageWidth = 640;
    _imageHeight = 480;
    _semanticImage.resize(_imageWidth * _imageHeight, 0);
    _confidenceImage.resize(_imageWidth * _imageHeight, 0.0f);
}

std::string VROARSceneSemantics::getLabelName(VROARSemanticLabel label) const {
    switch (label) {
        case VROARSemanticLabel::UNLABELED: return "Unlabeled";
        case VROARSemanticLabel::SKY: return "Sky";
        case VROARSemanticLabel::BUILDING: return "Building";
        case VROARSemanticLabel::TREE: return "Tree";
        case VROARSemanticLabel::ROAD: return "Road";
        case VROARSemanticLabel::SIDEWALK: return "Sidewalk";
        case VROARSemanticLabel::TERRAIN: return "Terrain";
        case VROARSemanticLabel::STRUCTURE: return "Structure";
        case VROARSemanticLabel::OBJECT: return "Object";
        case VROARSemanticLabel::VEHICLE: return "Vehicle";
        case VROARSemanticLabel::PERSON: return "Person";
        case VROARSemanticLabel::WATER: return "Water";
        case VROARSemanticLabel::VEGETATION: return "Vegetation";
        case VROARSemanticLabel::CAR: return "Car";
        case VROARSemanticLabel::TRUCK: return "Truck";
        case VROARSemanticLabel::BUS: return "Bus";
        case VROARSemanticLabel::MOTORCYCLE: return "Motorcycle";
        case VROARSemanticLabel::BICYCLE: return "Bicycle";
        case VROARSemanticLabel::TRAFFIC_LIGHT: return "Traffic Light";
        case VROARSemanticLabel::TRAFFIC_SIGN: return "Traffic Sign";
        case VROARSemanticLabel::FENCE: return "Fence";
        default: return "Unknown";
    }
}