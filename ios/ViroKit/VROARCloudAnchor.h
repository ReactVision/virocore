//
//  VROARCloudAnchor.h
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#ifndef VROARCloudAnchor_h
#define VROARCloudAnchor_h

#include "VROARAnchor.h"
#include <string>
#include <functional>
#include <memory>

enum class VROCloudAnchorState {
    NONE,
    TASK_IN_PROGRESS,
    SUCCESS,
    ERROR_INTERNAL,
    ERROR_NOT_AUTHORIZED,
    ERROR_RESOURCE_EXHAUSTED,
    ERROR_HOSTING_DATASET_PROCESSING_FAILED,
    ERROR_CLOUD_ID_NOT_FOUND,
    ERROR_RESOLVING_LOCALIZATION_NO_MATCH,
    ERROR_RESOLVING_SDK_VERSION_TOO_OLD,
    ERROR_RESOLVING_SDK_VERSION_TOO_NEW
};

enum class VROCloudAnchorProvider {
    NONE,
    GOOGLE
};

class VROARCloudAnchor : public VROARAnchor {
public:
    VROARCloudAnchor(const std::string& cloudAnchorId = "");
    virtual ~VROARCloudAnchor();
    
    // Cloud anchor properties
    std::string getCloudAnchorId() const { return _cloudAnchorId; }
    void setCloudAnchorId(const std::string& cloudAnchorId) { _cloudAnchorId = cloudAnchorId; }
    
    VROCloudAnchorState getCloudAnchorState() const { return _state; }
    void setCloudAnchorState(VROCloudAnchorState state) { _state = state; }
    
    // Hosting and resolving
    static void hostCloudAnchor(std::shared_ptr<VROARAnchor> localAnchor,
                               std::function<void(const std::string&, VROCloudAnchorState)> callback);
    
    static void resolveCloudAnchor(const std::string& cloudAnchorId,
                                  std::function<void(std::shared_ptr<VROARCloudAnchor>, VROCloudAnchorState)> callback);
    
    // Provider management
    static void setCloudAnchorProvider(VROCloudAnchorProvider provider);
    static VROCloudAnchorProvider getCloudAnchorProvider();
    
    // Update from ARCore cloud anchor
    void updateFromARCore(void* garCloudAnchor);
    
    // Utility methods
    static std::string getStateDescription(VROCloudAnchorState state);
    bool isHosted() const { return _state == VROCloudAnchorState::SUCCESS && !_cloudAnchorId.empty(); }
    
private:
    std::string _cloudAnchorId;
    VROCloudAnchorState _state;
    void* _garCloudAnchor; // Reference to GARCloudAnchor
    
    static VROCloudAnchorProvider _provider;
};

#endif /* VROARCloudAnchor_h */