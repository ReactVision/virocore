//
//  VROARMemoryManager.cpp
//  ViroRenderer
//
//  Advanced memory management implementation for AR features
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARMemoryManager.h"
#include "VROARGeospatialAnchor.h"
#include "VROARSceneSemantics.h"
#include "VROARAugmentedFace.h"
#include "VROTexture.h"
#include "VROGeometry.h"
#include "VROLog.h"
#include <algorithm>
#include <cstdlib>

std::shared_ptr<VROARMemoryManager> VROARMemoryManager::_instance = nullptr;
std::mutex VROARMemoryManager::_instanceMutex;

std::shared_ptr<VROARMemoryManager> VROARMemoryManager::getInstance() {
    std::lock_guard<std::mutex> lock(_instanceMutex);
    if (!_instance) {
        // Use a custom deleter to access private destructor
        _instance = std::shared_ptr<VROARMemoryManager>(
            new VROARMemoryManager(), 
            [](VROARMemoryManager* p) { delete p; }
        );
    }
    return _instance;
}

VROARMemoryManager::VROARMemoryManager() :
    _lastGC(std::chrono::high_resolution_clock::now()),
    _lastCacheCleanup(std::chrono::high_resolution_clock::now()) {
    
    pinfo("AR Memory Manager initialized with %zu MB limit", _memoryLimit / (1024 * 1024));
}

VROARMemoryManager::~VROARMemoryManager() {
    clearUnusedCaches();
    pinfo("AR Memory Manager destroyed");
}

void VROARMemoryManager::registerAnchor(std::shared_ptr<VROARGeospatialAnchor> anchor) {
    if (!anchor) {
        return;
    }
    
    std::string id = anchor->getId();
    _anchors[id] = std::weak_ptr<VROARGeospatialAnchor>(anchor);
    
    updateStats();
    
    // Check memory pressure after registration
    if (getMemoryPressureLevel() > 0.8f) {
        handleMemoryPressure();
    }
}

void VROARMemoryManager::unregisterAnchor(const std::string& anchorId) {
    auto it = _anchors.find(anchorId);
    if (it != _anchors.end()) {
        _anchors.erase(it);
        updateStats();
    }
}

void VROARMemoryManager::registerSemantics(std::shared_ptr<VROARSceneSemantics> semantics) {
    if (!semantics) {
        return;
    }
    
    std::string id = semantics->getId();
    _semantics[id] = std::weak_ptr<VROARSceneSemantics>(semantics);
    
    updateStats();
    
    // Check memory pressure after registration
    if (getMemoryPressureLevel() > 0.8f) {
        handleMemoryPressure();
    }
}

void VROARMemoryManager::unregisterSemantics(const std::string& semanticsId) {
    auto it = _semantics.find(semanticsId);
    if (it != _semantics.end()) {
        _semantics.erase(it);
        updateStats();
    }
}

void VROARMemoryManager::registerFace(std::shared_ptr<VROARAugmentedFace> face) {
    if (!face) {
        return;
    }
    
    std::string id = face->getId();
    _faces[id] = std::weak_ptr<VROARAugmentedFace>(face);
    
    updateStats();
    
    // Check memory pressure after registration
    if (getMemoryPressureLevel() > 0.8f) {
        handleMemoryPressure();
    }
}

void VROARMemoryManager::unregisterFace(const std::string& faceId) {
    auto it = _faces.find(faceId);
    if (it != _faces.end()) {
        _faces.erase(it);
        updateStats();
    }
}

void VROARMemoryManager::cacheTexture(const std::string& key, std::shared_ptr<VROTexture> texture) {
    if (!texture) {
        return;
    }
    
    // Estimate texture size (simplified)
    size_t textureSize = 1024 * 1024; // 1MB default estimate
    
    // Check if we need to evict entries to make room
    size_t currentCacheSize = 0;
    for (const auto& pair : _textureCache) {
        currentCacheSize += pair.second.size;
    }
    
    if (currentCacheSize + textureSize > MAX_TEXTURE_CACHE_SIZE) {
        evictLRUCacheEntries(MAX_TEXTURE_CACHE_SIZE - textureSize);
    }
    
    CacheEntry entry;
    entry.object = std::static_pointer_cast<void>(texture);
    entry.lastAccessed = std::chrono::high_resolution_clock::now();
    entry.size = textureSize;
    
    _textureCache[key] = entry;
    updateStats();
}

std::shared_ptr<VROTexture> VROARMemoryManager::getCachedTexture(const std::string& key) {
    auto it = _textureCache.find(key);
    if (it != _textureCache.end()) {
        it->second.lastAccessed = std::chrono::high_resolution_clock::now();
        return std::static_pointer_cast<VROTexture>(it->second.object);
    }
    return nullptr;
}

void VROARMemoryManager::cacheGeometry(const std::string& key, std::shared_ptr<VROGeometry> geometry) {
    if (!geometry) {
        return;
    }
    
    // Estimate geometry size (simplified)
    size_t geometrySize = 512 * 1024; // 512KB default estimate
    
    // Check if we need to evict entries to make room
    size_t currentCacheSize = 0;
    for (const auto& pair : _geometryCache) {
        currentCacheSize += pair.second.size;
    }
    
    if (currentCacheSize + geometrySize > MAX_GEOMETRY_CACHE_SIZE) {
        evictLRUCacheEntries(MAX_GEOMETRY_CACHE_SIZE - geometrySize);
    }
    
    CacheEntry entry;
    entry.object = std::static_pointer_cast<void>(geometry);
    entry.lastAccessed = std::chrono::high_resolution_clock::now();
    entry.size = geometrySize;
    
    _geometryCache[key] = entry;
    updateStats();
}

std::shared_ptr<VROGeometry> VROARMemoryManager::getCachedGeometry(const std::string& key) {
    auto it = _geometryCache.find(key);
    if (it != _geometryCache.end()) {
        it->second.lastAccessed = std::chrono::high_resolution_clock::now();
        return std::static_pointer_cast<VROGeometry>(it->second.object);
    }
    return nullptr;
}

void VROARMemoryManager::performGarbageCollection() {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    cleanupExpiredObjects();
    clearUnusedCaches();
    
    _lastGC = std::chrono::high_resolution_clock::now();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    double gcTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _stats.lastGCTime = gcTime;
    }
    
    updateStats();
    
    pinfo("AR Memory Manager GC completed in %.2f ms, freed memory", gcTime);
}

void VROARMemoryManager::clearUnusedCaches() {
    auto now = std::chrono::high_resolution_clock::now();
    
    // Clear expired texture cache entries
    for (auto it = _textureCache.begin(); it != _textureCache.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastAccessed);
        if (elapsed.count() > CACHE_ENTRY_TTL_SECONDS) {
            it = _textureCache.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clear expired geometry cache entries
    for (auto it = _geometryCache.begin(); it != _geometryCache.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastAccessed);
        if (elapsed.count() > CACHE_ENTRY_TTL_SECONDS) {
            it = _geometryCache.erase(it);
        } else {
            ++it;
        }
    }
    
    _lastCacheCleanup = now;
}

void VROARMemoryManager::compactMemory() {
    // Perform aggressive cleanup
    performGarbageCollection();
    
    // Force evict half the cache entries
    evictLRUCacheEntries(MAX_TEXTURE_CACHE_SIZE / 2);
    
    // Clear all caches if memory pressure is critical
    if (getMemoryPressureLevel() > 0.9f) {
        _textureCache.clear();
        _geometryCache.clear();
        pinfo("AR Memory Manager: Cleared all caches due to critical memory pressure");
    }
    
    updateStats();
}

size_t VROARMemoryManager::getMemoryUsage() const {
    updateStats();
    return _stats.totalAllocated;
}

void VROARMemoryManager::setMemoryLimit(size_t limitMB) {
    _memoryLimit = limitMB * 1024 * 1024;
    pinfo("AR Memory Manager limit set to %zu MB", limitMB);
    
    // If current usage exceeds new limit, perform cleanup
    if (getMemoryUsage() > _memoryLimit) {
        handleMemoryPressure();
    }
}

void VROARMemoryManager::handleMemoryPressure() {
    float pressureLevel = getMemoryPressureLevel();
    
    pinfo("AR Memory Manager handling memory pressure: %.1f%%", pressureLevel * 100.0f);
    
    if (pressureLevel > 0.7f) {
        clearUnusedCaches();
    }
    
    if (pressureLevel > 0.8f) {
        evictLRUCacheEntries(_stats.textureCache / 2 + _stats.geometryCache / 2);
    }
    
    if (pressureLevel > 0.9f) {
        compactMemory();
    }
    
    // Notify callback if set
    if (_memoryPressureCallback) {
        _memoryPressureCallback(pressureLevel);
    }
}

void VROARMemoryManager::setMemoryPressureCallback(std::function<void(float)> callback) {
    _memoryPressureCallback = callback;
}

float VROARMemoryManager::getMemoryPressureLevel() const {
    size_t currentUsage = getMemoryUsage();
    return static_cast<float>(currentUsage) / static_cast<float>(_memoryLimit);
}

void VROARMemoryManager::resetStats() {
    std::lock_guard<std::mutex> lock(_statsMutex);
    _stats = MemoryStats();
}

void VROARMemoryManager::updateStats() const {
    std::lock_guard<std::mutex> lock(_statsMutex);
    
    // Count active objects
    _stats.activeAnchors = 0;
    _stats.anchorMemory = 0;
    for (const auto& pair : _anchors) {
        auto anchor = pair.second.lock();
        if (anchor) {
            _stats.activeAnchors++;
            _stats.anchorMemory += estimateObjectSize(anchor);
        }
    }
    
    _stats.activeSemantics = 0;
    _stats.semanticsMemory = 0;
    for (const auto& pair : _semantics) {
        auto semantics = pair.second.lock();
        if (semantics) {
            _stats.activeSemantics++;
            _stats.semanticsMemory += estimateObjectSize(semantics);
        }
    }
    
    _stats.activeFaces = 0;
    _stats.faceMemory = 0;
    for (const auto& pair : _faces) {
        auto face = pair.second.lock();
        if (face) {
            _stats.activeFaces++;
            _stats.faceMemory += estimateObjectSize(face);
        }
    }
    
    // Calculate cache memory usage
    _stats.textureCache = 0;
    for (const auto& pair : _textureCache) {
        _stats.textureCache += pair.second.size;
    }
    
    _stats.geometryCache = 0;
    for (const auto& pair : _geometryCache) {
        _stats.geometryCache += pair.second.size;
    }
    
    _stats.totalAllocated = _stats.anchorMemory + _stats.semanticsMemory + 
                           _stats.faceMemory + _stats.textureCache + _stats.geometryCache;
}

void VROARMemoryManager::cleanupExpiredObjects() {
    // Clean up expired weak pointers
    for (auto it = _anchors.begin(); it != _anchors.end();) {
        if (it->second.expired()) {
            it = _anchors.erase(it);
        } else {
            ++it;
        }
    }
    
    for (auto it = _semantics.begin(); it != _semantics.end();) {
        if (it->second.expired()) {
            it = _semantics.erase(it);
        } else {
            ++it;
        }
    }
    
    for (auto it = _faces.begin(); it != _faces.end();) {
        if (it->second.expired()) {
            it = _faces.erase(it);
        } else {
            ++it;
        }
    }
}

void VROARMemoryManager::evictLRUCacheEntries(size_t targetSize) {
    // Collect all cache entries with their access times
    std::vector<std::pair<std::string, std::chrono::high_resolution_clock::time_point>> entries;
    
    for (const auto& pair : _textureCache) {
        entries.push_back({pair.first, pair.second.lastAccessed});
    }
    
    for (const auto& pair : _geometryCache) {
        entries.push_back({pair.first, pair.second.lastAccessed});
    }
    
    // Sort by access time (oldest first)
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    
    // Evict entries until we reach target size
    size_t evicted = 0;
    for (const auto& entry : entries) {
        if (evicted >= targetSize) {
            break;
        }
        
        auto texIt = _textureCache.find(entry.first);
        if (texIt != _textureCache.end()) {
            evicted += texIt->second.size;
            _textureCache.erase(texIt);
            continue;
        }
        
        auto geomIt = _geometryCache.find(entry.first);
        if (geomIt != _geometryCache.end()) {
            evicted += geomIt->second.size;
            _geometryCache.erase(geomIt);
        }
    }
}

size_t VROARMemoryManager::estimateObjectSize(std::shared_ptr<VROARGeospatialAnchor> anchor) const {
    // Simplified estimation - would be more accurate with actual object introspection
    return 1024; // 1KB per anchor
}

size_t VROARMemoryManager::estimateObjectSize(std::shared_ptr<VROARSceneSemantics> semantics) const {
    // Simplified estimation - would depend on number of labels and their data
    return 4096; // 4KB per semantics object
}

size_t VROARMemoryManager::estimateObjectSize(std::shared_ptr<VROARAugmentedFace> face) const {
    // Simplified estimation - would depend on mesh complexity and blend shapes
    return 16384; // 16KB per face object
}

// Template specializations for auto memory managers
template<>
void VROARAutoMemoryManager<VROARGeospatialAnchor>::registerObject() {
    VROARMemoryManager::getInstance()->registerAnchor(_object);
}

template<>
void VROARAutoMemoryManager<VROARGeospatialAnchor>::unregisterObject() {
    VROARMemoryManager::getInstance()->unregisterAnchor(_id);
}

template<>
void VROARAutoMemoryManager<VROARSceneSemantics>::registerObject() {
    VROARMemoryManager::getInstance()->registerSemantics(_object);
}

template<>
void VROARAutoMemoryManager<VROARSceneSemantics>::unregisterObject() {
    VROARMemoryManager::getInstance()->unregisterSemantics(_id);
}

template<>
void VROARAutoMemoryManager<VROARAugmentedFace>::registerObject() {
    VROARMemoryManager::getInstance()->registerFace(_object);
}

template<>
void VROARAutoMemoryManager<VROARAugmentedFace>::unregisterObject() {
    VROARMemoryManager::getInstance()->unregisterFace(_id);
}