//
//  VROARMemoryManager.h
//  ViroRenderer
//
//  Advanced memory management for AR features
//  Copyright © 2024 Viro Media. All rights reserved.

#ifndef VROARMemoryManager_h
#define VROARMemoryManager_h

#include <memory>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <vector>
#include <functional>

class VROARGeospatialAnchor;
class VROARSceneSemantics;
class VROARAugmentedFace;
class VROTexture;
class VROGeometry;

// Advanced memory management for AR objects
class VROARMemoryManager {
public:
    static std::shared_ptr<VROARMemoryManager> getInstance();
    
    // Memory pool management
    struct MemoryPool {
        size_t totalSize = 0;
        size_t usedSize = 0;
        size_t peakUsage = 0;
        std::chrono::high_resolution_clock::time_point lastCleanup;
    };
    
    // Object lifecycle management
    void registerAnchor(std::shared_ptr<VROARGeospatialAnchor> anchor);
    void unregisterAnchor(const std::string& anchorId);
    void registerSemantics(std::shared_ptr<VROARSceneSemantics> semantics);
    void unregisterSemantics(const std::string& semanticsId);
    void registerFace(std::shared_ptr<VROARAugmentedFace> face);
    void unregisterFace(const std::string& faceId);
    
    // Cache management
    void cacheTexture(const std::string& key, std::shared_ptr<VROTexture> texture);
    std::shared_ptr<VROTexture> getCachedTexture(const std::string& key);
    void cacheGeometry(const std::string& key, std::shared_ptr<VROGeometry> geometry);
    std::shared_ptr<VROGeometry> getCachedGeometry(const std::string& key);
    
    // Memory optimization
    void performGarbageCollection();
    void clearUnusedCaches();
    void compactMemory();
    size_t getMemoryUsage() const;
    size_t getMemoryLimit() const { return _memoryLimit; }
    void setMemoryLimit(size_t limitMB);
    
    // Memory pressure handling
    void handleMemoryPressure();
    void setMemoryPressureCallback(std::function<void(float)> callback);
    float getMemoryPressureLevel() const;
    
    // Statistics
    struct MemoryStats {
        size_t totalAllocated = 0;
        size_t anchorMemory = 0;
        size_t semanticsMemory = 0;
        size_t faceMemory = 0;
        size_t textureCache = 0;
        size_t geometryCache = 0;
        int activeAnchors = 0;
        int activeSemantics = 0;
        int activeFaces = 0;
        double lastGCTime = 0.0;
    };
    
    const MemoryStats& getStats() const { return _stats; }
    void resetStats();
    
private:
    VROARMemoryManager();
    ~VROARMemoryManager();
    
    static std::shared_ptr<VROARMemoryManager> _instance;
    static std::mutex _instanceMutex;
    
    // Object tracking
    std::unordered_map<std::string, std::weak_ptr<VROARGeospatialAnchor>> _anchors;
    std::unordered_map<std::string, std::weak_ptr<VROARSceneSemantics>> _semantics;
    std::unordered_map<std::string, std::weak_ptr<VROARAugmentedFace>> _faces;
    
    // Cache management
    struct CacheEntry {
        std::shared_ptr<void> object;
        std::chrono::high_resolution_clock::time_point lastAccessed;
        size_t size;
    };
    
    std::unordered_map<std::string, CacheEntry> _textureCache;
    std::unordered_map<std::string, CacheEntry> _geometryCache;
    
    // Memory management
    size_t _memoryLimit = 512 * 1024 * 1024; // 512MB default
    std::chrono::high_resolution_clock::time_point _lastGC;
    std::chrono::high_resolution_clock::time_point _lastCacheCleanup;
    std::function<void(float)> _memoryPressureCallback;
    
    // Statistics
    mutable MemoryStats _stats;
    mutable std::mutex _statsMutex;
    
    // Helper methods
    void updateStats() const;
    void cleanupExpiredObjects();
    void evictLRUCacheEntries(size_t targetSize);
    size_t estimateObjectSize(std::shared_ptr<VROARGeospatialAnchor> anchor) const;
    size_t estimateObjectSize(std::shared_ptr<VROARSceneSemantics> semantics) const;
    size_t estimateObjectSize(std::shared_ptr<VROARAugmentedFace> face) const;
    
    // Constants
    static const size_t MAX_TEXTURE_CACHE_SIZE = 64 * 1024 * 1024; // 64MB
    static const size_t MAX_GEOMETRY_CACHE_SIZE = 32 * 1024 * 1024; // 32MB
    static const int GC_INTERVAL_SECONDS = 30;
    static const int CACHE_CLEANUP_INTERVAL_SECONDS = 60;
    static const int CACHE_ENTRY_TTL_SECONDS = 300; // 5 minutes
};

// RAII helper for automatic memory management
template<typename T>
class VROARAutoMemoryManager {
public:
    VROARAutoMemoryManager(std::shared_ptr<T> object, const std::string& id) 
        : _object(object), _id(id) {
        registerObject();
    }
    
    ~VROARAutoMemoryManager() {
        unregisterObject();
    }
    
    std::shared_ptr<T> get() const { return _object; }
    const std::string& getId() const { return _id; }
    
private:
    std::shared_ptr<T> _object;
    std::string _id;
    
    void registerObject();
    void unregisterObject();
};

// Specializations for different object types
using VROARAutoAnchorManager = VROARAutoMemoryManager<VROARGeospatialAnchor>;
using VROARAutoSemanticsManager = VROARAutoMemoryManager<VROARSceneSemantics>;
using VROARAutoFaceManager = VROARAutoMemoryManager<VROARAugmentedFace>;

#endif /* VROARMemoryManager_h */