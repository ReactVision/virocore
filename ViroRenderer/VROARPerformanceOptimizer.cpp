//
//  VROARPerformanceOptimizer.cpp
//  ViroRenderer
//
//  Performance optimization implementation for AR features
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARPerformanceOptimizer.h"
#include "VROARFrame.h"
#include "VROARGeospatialAnchor.h"
#include "VROARSceneSemantics.h"
#include "VROARAugmentedFace.h"
#include "VROLog.h"
#include "VROMath.h"
#include <algorithm>
#include <cmath>

VROARPerformanceOptimizer::VROARPerformanceOptimizer() :
    _lastFrameTime(std::chrono::high_resolution_clock::now()),
    _lastMemoryCheck(std::chrono::high_resolution_clock::now()),
    _lastFrameProcessTime(std::chrono::high_resolution_clock::now()),
    _lastSemanticsUpdate(std::chrono::high_resolution_clock::now()) {
    
    _frameTimeHistory.reserve(MAX_FRAME_HISTORY);
    pinfo("AR Performance Optimizer initialized with %d MB memory budget", (int)_memoryBudgetMB);
}

VROARPerformanceOptimizer::~VROARPerformanceOptimizer() {
    performMemoryCleanup();
}

bool VROARPerformanceOptimizer::shouldProcessFrame(std::shared_ptr<VROARFrame> frame) {
    if (!frame) {
        return false;
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastFrameProcessTime);
    
    // Skip frames if we're processing too fast and performance is poor
    if (_adaptiveQualityEnabled && _currentQualityLevel < 3) {
        int minInterval = MIN_FRAME_INTERVAL_MS * (4 - _currentQualityLevel);
        if (elapsed.count() < minInterval) {
            _metrics.droppedFrameCount++;
            return false;
        }
    }
    
    _lastFrameProcessTime = now;
    return true;
}

void VROARPerformanceOptimizer::optimizeFrameProcessing(std::shared_ptr<VROARFrame> frame) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Update memory usage periodically
    auto now = std::chrono::high_resolution_clock::now();
    auto memoryCheckElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastMemoryCheck);
    if (memoryCheckElapsed.count() > 1000) { // Check every second
        _metrics.memoryUsageMB = getCurrentMemoryUsage();
        _lastMemoryCheck = now;
        
        if (isMemoryBudgetExceeded()) {
            performMemoryCleanup();
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    _metrics.frameProcessingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    updateFrameTimeHistory(_metrics.frameProcessingTime);
    updateMetrics();
}

void VROARPerformanceOptimizer::optimizeAnchorUpdates(std::vector<std::shared_ptr<VROARGeospatialAnchor>>& anchors) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Remove anchors that don't need updates
    anchors.erase(
        std::remove_if(anchors.begin(), anchors.end(),
            [this](const std::shared_ptr<VROARGeospatialAnchor>& anchor) {
                return !shouldUpdateAnchor(anchor);
            }), anchors.end());
    
    // Sort anchors by distance for LOD processing
    std::sort(anchors.begin(), anchors.end(),
        [](const std::shared_ptr<VROARGeospatialAnchor>& a, const std::shared_ptr<VROARGeospatialAnchor>& b) {
            // Implement distance-based sorting here
            return true; // Placeholder
        });
    
    auto endTime = std::chrono::high_resolution_clock::now();
    _metrics.anchorResolutionTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
}

bool VROARPerformanceOptimizer::shouldUpdateAnchor(std::shared_ptr<VROARGeospatialAnchor> anchor) {
    if (!anchor) {
        return false;
    }
    
    std::string anchorId = anchor->getId();
    auto now = std::chrono::high_resolution_clock::now();
    
    auto it = _anchorLastUpdateTimes.find(anchorId);
    if (it == _anchorLastUpdateTimes.end()) {
        _anchorLastUpdateTimes[anchorId] = now;
        return true;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
    bool shouldUpdate = elapsed.count() >= ANCHOR_UPDATE_INTERVAL_MS;
    
    if (shouldUpdate) {
        _anchorLastUpdateTimes[anchorId] = now;
    }
    
    return shouldUpdate;
}

void VROARPerformanceOptimizer::cullDistantAnchors(std::vector<std::shared_ptr<VROARGeospatialAnchor>>& anchors, 
                                                  const VROVector3f& cameraPosition, float maxDistance) {
    anchors.erase(
        std::remove_if(anchors.begin(), anchors.end(),
            [cameraPosition, maxDistance](const std::shared_ptr<VROARGeospatialAnchor>& anchor) {
                if (!anchor) return true;
                
                // Convert lat/lon to approximate world coordinates for distance calculation
                // This is a simplified calculation
                VROVector3f anchorPos(anchor->getLongitude(), anchor->getLatitude(), anchor->getAltitude());
                float distance = cameraPosition.distance(anchorPos);
                return distance > maxDistance;
            }), anchors.end());
}

void VROARPerformanceOptimizer::optimizeSemanticsProcessing(std::shared_ptr<VROARSceneSemantics> semantics) {
    if (!shouldUpdateSemantics(semantics)) {
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Limit the number of semantic labels processed
    if (_adaptiveQualityEnabled) {
        int maxLabels = 5 + (_currentQualityLevel * 3); // 5-20 labels based on quality
        limitSemanticLabels(semantics, maxLabels);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    _metrics.semanticsProcessingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
}

bool VROARPerformanceOptimizer::shouldUpdateSemantics(std::shared_ptr<VROARSceneSemantics> semantics) {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastSemanticsUpdate);
    
    bool shouldUpdate = elapsed.count() >= SEMANTICS_UPDATE_INTERVAL_MS;
    if (shouldUpdate) {
        _lastSemanticsUpdate = now;
    }
    
    return shouldUpdate;
}

void VROARPerformanceOptimizer::limitSemanticLabels(std::shared_ptr<VROARSceneSemantics> semantics, int maxLabels) {
    if (!semantics) {
        return;
    }
    
    // Simply disable semantics processing if too complex
    // In a real implementation, this would analyze semantic complexity
    // and selectively disable heavy processing
    if (maxLabels < 10) {
        semantics->setEnabled(false);
    }
    
    // Update semantics object with limited labels
    // (Implementation would depend on the semantics class interface)
}

void VROARPerformanceOptimizer::optimizeFaceTracking(std::shared_ptr<VROARAugmentedFace> face) {
    if (!shouldUpdateFaceMesh(face)) {
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Reduce mesh complexity for performance
    if (_adaptiveQualityEnabled && _currentQualityLevel < 4) {
        float reductionFactor = 1.0f - (_currentQualityLevel * 0.1f); // 0.6-1.0 based on quality
        reduceFaceMeshComplexity(face, reductionFactor);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    _metrics.faceTrackingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
}

bool VROARPerformanceOptimizer::shouldUpdateFaceMesh(std::shared_ptr<VROARAugmentedFace> face) {
    if (!face) {
        return false;
    }
    
    std::string faceId = face->getId();
    auto now = std::chrono::high_resolution_clock::now();
    
    auto it = _faceLastUpdateTimes.find(faceId);
    if (it == _faceLastUpdateTimes.end()) {
        _faceLastUpdateTimes[faceId] = now;
        return true;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
    bool shouldUpdate = elapsed.count() >= FACE_UPDATE_INTERVAL_MS;
    
    if (shouldUpdate) {
        _faceLastUpdateTimes[faceId] = now;
    }
    
    return shouldUpdate;
}

void VROARPerformanceOptimizer::reduceFaceMeshComplexity(std::shared_ptr<VROARAugmentedFace> face, float reductionFactor) {
    if (!face || reductionFactor >= 1.0f) {
        return;
    }
    
    // Implement mesh decimation/simplification
    // This is a placeholder - actual implementation would require mesh processing algorithms
    pinfo("Reducing face mesh complexity by factor %.2f", reductionFactor);
}

void VROARPerformanceOptimizer::performMemoryCleanup() {
    // Clean up old anchor update times
    auto now = std::chrono::high_resolution_clock::now();
    for (auto it = _anchorLastUpdateTimes.begin(); it != _anchorLastUpdateTimes.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
        if (elapsed.count() > 300) { // Remove entries older than 5 minutes
            it = _anchorLastUpdateTimes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clean up old face update times
    for (auto it = _faceLastUpdateTimes.begin(); it != _faceLastUpdateTimes.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
        if (elapsed.count() > 300) { // Remove entries older than 5 minutes
            it = _faceLastUpdateTimes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clear frame time history if too large
    if (_frameTimeHistory.size() > MAX_FRAME_HISTORY * 2) {
        _frameTimeHistory.erase(_frameTimeHistory.begin(), 
                               _frameTimeHistory.begin() + MAX_FRAME_HISTORY);
    }
    
    pinfo("AR Performance Optimizer memory cleanup completed");
}

size_t VROARPerformanceOptimizer::getCurrentMemoryUsage() const {
    // Platform-specific memory usage calculation would go here
    // This is a placeholder implementation
    return 128; // Mock 128MB usage
}

bool VROARPerformanceOptimizer::isMemoryBudgetExceeded() const {
    return _metrics.memoryUsageMB > _memoryBudgetMB;
}

void VROARPerformanceOptimizer::resetMetrics() {
    _metrics = PerformanceMetrics();
    _frameTimeHistory.clear();
}

void VROARPerformanceOptimizer::updateMetrics() {
    // Calculate average FPS from frame time history
    if (!_frameTimeHistory.empty()) {
        double avgFrameTime = 0.0;
        for (double time : _frameTimeHistory) {
            avgFrameTime += time;
        }
        avgFrameTime /= _frameTimeHistory.size();
        _metrics.averageFPS = avgFrameTime > 0.0 ? 1000.0 / avgFrameTime : 0.0;
    }
    
    // Update adaptive quality if enabled
    if (_adaptiveQualityEnabled) {
        adjustQualityBasedOnPerformance();
    }
}

void VROARPerformanceOptimizer::adjustQualityBasedOnPerformance() {
    // Target 30fps minimum
    const double TARGET_FPS = 30.0;
    const double FPS_TOLERANCE = 5.0;
    
    if (_metrics.averageFPS < TARGET_FPS - FPS_TOLERANCE) {
        // Performance is poor, reduce quality
        if (_currentQualityLevel > 0) {
            _currentQualityLevel--;
            pinfo("Reducing AR quality level to %d due to low FPS (%.1f)", _currentQualityLevel, _metrics.averageFPS);
        }
    } else if (_metrics.averageFPS > TARGET_FPS + FPS_TOLERANCE && _currentQualityLevel < 5) {
        // Performance is good, increase quality
        _currentQualityLevel++;
        pinfo("Increasing AR quality level to %d with FPS %.1f", _currentQualityLevel, _metrics.averageFPS);
    }
    
    // Also consider memory usage
    if (isMemoryBudgetExceeded() && _currentQualityLevel > 1) {
        _currentQualityLevel--;
        pinfo("Reducing AR quality level to %d due to memory pressure (%zu MB)", 
              _currentQualityLevel, _metrics.memoryUsageMB);
    }
}

double VROARPerformanceOptimizer::getCurrentTimeMs() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto epoch = now.time_since_epoch();
    return std::chrono::duration<double, std::milli>(epoch).count();
}

void VROARPerformanceOptimizer::updateFrameTimeHistory(double frameTime) {
    _frameTimeHistory.push_back(frameTime);
    if (_frameTimeHistory.size() > MAX_FRAME_HISTORY) {
        _frameTimeHistory.erase(_frameTimeHistory.begin());
    }
}