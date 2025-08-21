//
//  VROARPerformanceOptimizer.h
//  ViroRenderer
//
//  Performance optimization utilities for AR features
//  Copyright © 2024 Viro Media. All rights reserved.

#ifndef VROARPerformanceOptimizer_h
#define VROARPerformanceOptimizer_h

#include <memory>
#include <vector>
#include <chrono>
#include <unordered_map>
#include "VROVector3f.h"
#include "VROMatrix4f.h"

class VROARFrame;
class VROARGeospatialAnchor;
class VROARSceneSemantics;
class VROARAugmentedFace;

// Performance monitoring and optimization for AR features
class VROARPerformanceOptimizer {
public:
    VROARPerformanceOptimizer();
    ~VROARPerformanceOptimizer();
    
    // Performance monitoring
    struct PerformanceMetrics {
        double frameProcessingTime = 0.0;
        double anchorResolutionTime = 0.0;
        double semanticsProcessingTime = 0.0;
        double faceTrackingTime = 0.0;
        size_t memoryUsageMB = 0;
        int droppedFrameCount = 0;
        double averageFPS = 0.0;
    };
    
    // Frame processing optimization
    bool shouldProcessFrame(std::shared_ptr<VROARFrame> frame);
    void optimizeFrameProcessing(std::shared_ptr<VROARFrame> frame);
    
    // Anchor management optimization
    void optimizeAnchorUpdates(std::vector<std::shared_ptr<VROARGeospatialAnchor>>& anchors);
    bool shouldUpdateAnchor(std::shared_ptr<VROARGeospatialAnchor> anchor);
    void cullDistantAnchors(std::vector<std::shared_ptr<VROARGeospatialAnchor>>& anchors, 
                           const VROVector3f& cameraPosition, float maxDistance = 100.0f);
    
    // Scene semantics optimization
    void optimizeSemanticsProcessing(std::shared_ptr<VROARSceneSemantics> semantics);
    bool shouldUpdateSemantics(std::shared_ptr<VROARSceneSemantics> semantics);
    void limitSemanticLabels(std::shared_ptr<VROARSceneSemantics> semantics, int maxLabels = 10);
    
    // Face tracking optimization
    void optimizeFaceTracking(std::shared_ptr<VROARAugmentedFace> face);
    bool shouldUpdateFaceMesh(std::shared_ptr<VROARAugmentedFace> face);
    void reduceFaceMeshComplexity(std::shared_ptr<VROARAugmentedFace> face, float reductionFactor = 0.5f);
    
    // Memory management
    void performMemoryCleanup();
    size_t getCurrentMemoryUsage() const;
    void setMemoryBudgetMB(size_t budgetMB) { _memoryBudgetMB = budgetMB; }
    bool isMemoryBudgetExceeded() const;
    
    // Performance metrics
    const PerformanceMetrics& getMetrics() const { return _metrics; }
    void resetMetrics();
    void updateMetrics();
    
    // Adaptive quality settings
    void setAdaptiveQuality(bool enabled) { _adaptiveQualityEnabled = enabled; }
    bool isAdaptiveQualityEnabled() const { return _adaptiveQualityEnabled; }
    void adjustQualityBasedOnPerformance();
    
private:
    // Performance tracking
    PerformanceMetrics _metrics;
    std::chrono::high_resolution_clock::time_point _lastFrameTime;
    std::vector<double> _frameTimeHistory;
    static const size_t MAX_FRAME_HISTORY = 60; // 1 second at 60fps
    
    // Memory management
    size_t _memoryBudgetMB = 512; // Default 512MB budget
    std::chrono::high_resolution_clock::time_point _lastMemoryCheck;
    
    // Adaptive quality
    bool _adaptiveQualityEnabled = true;
    int _currentQualityLevel = 3; // 0=lowest, 5=highest
    
    // Frame processing optimization
    std::chrono::high_resolution_clock::time_point _lastFrameProcessTime;
    static const int MIN_FRAME_INTERVAL_MS = 33; // ~30fps minimum
    
    // Anchor tracking
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> _anchorLastUpdateTimes;
    static const int ANCHOR_UPDATE_INTERVAL_MS = 100; // Update anchors max 10fps
    
    // Semantics tracking
    std::chrono::high_resolution_clock::time_point _lastSemanticsUpdate;
    static const int SEMANTICS_UPDATE_INTERVAL_MS = 500; // Update semantics max 2fps
    
    // Face tracking
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> _faceLastUpdateTimes;
    static const int FACE_UPDATE_INTERVAL_MS = 50; // Update faces max 20fps
    
    // Helper methods
    double getCurrentTimeMs() const;
    void updateFrameTimeHistory(double frameTime);
    void adjustMemoryUsage();
    void adjustProcessingFrequency();
    void adjustMeshComplexity();
};

#endif /* VROARPerformanceOptimizer_h */