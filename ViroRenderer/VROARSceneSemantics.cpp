//
//  VROARSceneSemantics.cpp
//  ViroRenderer
//
//  Implementation of Scene Semantics processing
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARSceneSemantics.h"
#include "VROTexture.h"
#include "VROTextureSubstrate.h"
#include "VROLog.h"
#include "VROTime.h"
#include "VROVector4f.h"
#include <algorithm>
#include <cstring>

VROARSemanticLabel VROARSceneSemantics::getLabelAtPixel(int x, int y) const {
    if (!_semanticsEnabled || _labelData.empty()) {
        return VROARSemanticLabel::Unknown;
    }
    
    int index = getPixelIndex(x, y);
    if (index < 0 || index >= static_cast<int>(_labelData.size())) {
        return VROARSemanticLabel::Unknown;
    }
    
    uint8_t labelId = _labelData[index];
    if (labelId >= static_cast<uint8_t>(VROARSemanticLabel::Car) + 1) {
        return VROARSemanticLabel::Unknown;
    }
    
    return static_cast<VROARSemanticLabel>(labelId);
}

float VROARSceneSemantics::getConfidenceForLabelAtPixel(VROARSemanticLabel label, int x, int y) const {
    if (!_semanticsEnabled || _confidenceData.empty()) {
        return 0.0f;
    }
    
    int index = getPixelIndex(x, y);
    if (index < 0) {
        return 0.0f;
    }
    
    // For multi-channel confidence data, we need to offset by label index
    int labelIndex = static_cast<int>(label);
    int confidenceIndex = index * 23 + labelIndex; // 23 semantic labels
    
    if (confidenceIndex >= static_cast<int>(_confidenceData.size())) {
        return 0.0f;
    }
    
    return _confidenceData[confidenceIndex];
}

std::vector<VROVector3f> VROARSceneSemantics::getPixelsForLabel(VROARSemanticLabel label, 
                                                                float minConfidence) const {
    std::vector<VROVector3f> pixels;
    
    if (!_semanticsEnabled || _labelData.empty() || _confidenceData.empty()) {
        return pixels;
    }
    
    uint8_t targetLabel = static_cast<uint8_t>(label);
    
    for (int y = 0; y < _imageHeight; ++y) {
        for (int x = 0; x < _imageWidth; ++x) {
            int index = getPixelIndex(x, y);
            
            if (_labelData[index] == targetLabel) {
                float confidence = getConfidenceForLabelAtPixel(label, x, y);
                if (confidence >= minConfidence) {
                    pixels.push_back(VROVector3f(static_cast<float>(x), static_cast<float>(y)));
                }
            }
        }
    }
    
    return pixels;
}

std::shared_ptr<VROTexture> VROARSceneSemantics::getConfidenceTexture(VROARSemanticLabel label) const {
    auto it = _confidenceTextures.find(label);
    if (it != _confidenceTextures.end()) {
        return it->second;
    }
    
    // Create confidence texture for this label
    if (_confidenceData.empty() || _imageWidth == 0 || _imageHeight == 0) {
        return nullptr;
    }
    
    std::vector<uint8_t> confidenceImage(_imageWidth * _imageHeight);
    int labelIndex = static_cast<int>(label);
    
    for (int i = 0; i < _imageWidth * _imageHeight; ++i) {
        int confidenceIndex = i * 23 + labelIndex; // 23 semantic labels
        if (confidenceIndex < static_cast<int>(_confidenceData.size())) {
            float confidence = _confidenceData[confidenceIndex];
            confidenceImage[i] = static_cast<uint8_t>(confidence * 255.0f);
        } else {
            confidenceImage[i] = 0;
        }
    }
    
    // Create texture from confidence data
    // Note: This would need a proper texture substrate implementation
    // For now, return nullptr as placeholder
    return nullptr;
}

bool VROARSceneSemantics::hasLabel(VROARSemanticLabel label, float minConfidence) const {
    if (!_semanticsEnabled || _labelData.empty()) {
        return false;
    }
    
    uint8_t targetLabel = static_cast<uint8_t>(label);
    
    for (int i = 0; i < static_cast<int>(_labelData.size()); ++i) {
        if (_labelData[i] == targetLabel) {
            int y = i / _imageWidth;
            int x = i % _imageWidth;
            float confidence = getConfidenceForLabelAtPixel(label, x, y);
            if (confidence >= minConfidence) {
                return true;
            }
        }
    }
    
    return false;
}

float VROARSceneSemantics::getLabelCoverage(VROARSemanticLabel label, float minConfidence) const {
    if (!_semanticsEnabled || _labelData.empty()) {
        return 0.0f;
    }
    
    int totalPixels = _imageWidth * _imageHeight;
    if (totalPixels == 0) {
        return 0.0f;
    }
    
    int matchingPixels = 0;
    uint8_t targetLabel = static_cast<uint8_t>(label);
    
    for (int i = 0; i < static_cast<int>(_labelData.size()); ++i) {
        if (_labelData[i] == targetLabel) {
            int y = i / _imageWidth;
            int x = i % _imageWidth;
            float confidence = getConfidenceForLabelAtPixel(label, x, y);
            if (confidence >= minConfidence) {
                matchingPixels++;
            }
        }
    }
    
    return static_cast<float>(matchingPixels) / static_cast<float>(totalPixels);
}

void VROARSceneSemantics::updateFromFrame(const uint8_t* labelData, 
                                         const float* confidenceData,
                                         int width, int height) {
    if (!labelData || !confidenceData || width <= 0 || height <= 0) {
        return;
    }
    
    _imageWidth = width;
    _imageHeight = height;
    
    // Copy label data
    int pixelCount = width * height;
    _labelData.resize(pixelCount);
    std::memcpy(_labelData.data(), labelData, pixelCount);
    
    // Copy confidence data (23 channels per pixel for all semantic labels)
    int confidenceDataSize = pixelCount * 23;
    _confidenceData.resize(confidenceDataSize);
    std::memcpy(_confidenceData.data(), confidenceData, confidenceDataSize * sizeof(float));
    
    // Update textures for visualization
    updateTextures();
}

VROVector3f VROARSceneSemantics::semanticToViewport(const VROVector3f& semanticCoord) const {
    // Apply transformation matrix to convert from semantic image coordinates
    // to viewport coordinates
    VROVector4f coord(semanticCoord.x, semanticCoord.y, 0, 1);
    VROVector4f transformed = _semanticToViewportTransform.multiply(coord);
    
    return VROVector3f(transformed.x, transformed.y);
}

VROVector3f VROARSceneSemantics::viewportToSemantic(const VROVector3f& viewportCoord) const {
    // Apply inverse transformation matrix
    VROVector4f coord(viewportCoord.x, viewportCoord.y, 0, 1);
    VROVector4f transformed = _viewportToSemanticTransform.multiply(coord);
    
    return VROVector3f(transformed.x, transformed.y);
}

void VROARSceneSemantics::updateTextures() {
    if (_labelData.empty() || _imageWidth == 0 || _imageHeight == 0) {
        return;
    }
    
    // Create label texture with color mapping
    std::vector<uint8_t> coloredLabelData(_imageWidth * _imageHeight * 3); // RGB
    
    for (int i = 0; i < _imageWidth * _imageHeight; ++i) {
        uint8_t label = _labelData[i];
        VROVector3f color = getLabelColor(static_cast<VROARSemanticLabel>(label));
        
        coloredLabelData[i * 3 + 0] = static_cast<uint8_t>(color.x * 255);
        coloredLabelData[i * 3 + 1] = static_cast<uint8_t>(color.y * 255);
        coloredLabelData[i * 3 + 2] = static_cast<uint8_t>(color.z * 255);
    }
    
    // Create texture from colored label data
    // Note: This would need proper texture substrate implementation
    // For now, we'll set it to nullptr as placeholder
    _labelTexture = nullptr;
    
    // Clear confidence texture cache to force regeneration
    _confidenceTextures.clear();
}

int VROARSceneSemantics::getPixelIndex(int x, int y) const {
    if (x < 0 || x >= _imageWidth || y < 0 || y >= _imageHeight) {
        return -1;
    }
    
    return y * _imageWidth + x;
}

VROVector3f VROARSceneSemantics::getLabelColor(VROARSemanticLabel label) const {
    // Define color mapping for each semantic label
    switch (label) {
        case VROARSemanticLabel::Sky:
            return VROVector3f(0.5f, 0.8f, 1.0f); // Light blue
        case VROARSemanticLabel::Building:
            return VROVector3f(0.6f, 0.6f, 0.6f); // Gray
        case VROARSemanticLabel::Tree:
            return VROVector3f(0.0f, 0.8f, 0.0f); // Green
        case VROARSemanticLabel::Road:
            return VROVector3f(0.3f, 0.3f, 0.3f); // Dark gray
        case VROARSemanticLabel::Sidewalk:
            return VROVector3f(0.8f, 0.8f, 0.8f); // Light gray
        case VROARSemanticLabel::Terrain:
            return VROVector3f(0.6f, 0.4f, 0.2f); // Brown
        case VROARSemanticLabel::Water:
            return VROVector3f(0.0f, 0.5f, 1.0f); // Blue
        case VROARSemanticLabel::Person:
            return VROVector3f(1.0f, 0.8f, 0.6f); // Skin tone
        case VROARSemanticLabel::Car:
            return VROVector3f(1.0f, 0.0f, 0.0f); // Red
        case VROARSemanticLabel::Unknown:
        default:
            return VROVector3f(0.0f, 0.0f, 0.0f); // Black
    }
}