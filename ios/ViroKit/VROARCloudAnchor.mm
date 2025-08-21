//
//  VROARCloudAnchor.mm
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#import "VROARCloudAnchor.h"
#import <ARCore/ARCore.h>

VROCloudAnchorProvider VROARCloudAnchor::_provider = VROCloudAnchorProvider::NONE;

VROARCloudAnchor::VROARCloudAnchor(const std::string& cloudAnchorId) :
    VROARAnchor("cloud_anchor", VROMatrix4f::identity()),
    _cloudAnchorId(cloudAnchorId),
    _state(VROCloudAnchorState::NONE),
    _garCloudAnchor(nullptr) {
}

VROARCloudAnchor::~VROARCloudAnchor() {
    if (_garCloudAnchor) {
        _garCloudAnchor = nullptr;
    }
}

void VROARCloudAnchor::hostCloudAnchor(std::shared_ptr<VROARAnchor> localAnchor,
                                      std::function<void(const std::string&, VROCloudAnchorState)> callback) {
    if (_provider == VROCloudAnchorProvider::NONE) {
        callback("", VROCloudAnchorState::ERROR_NOT_AUTHORIZED);
        return;
    }
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // In real implementation, this would call ARCore cloud anchor hosting
        // GARSession *session = [GARSession sharedSession];
        // [session hostCloudAnchor:localAnchor completionHandler:callback];
        
        // Placeholder implementation
        NSString *cloudAnchorId = [[NSUUID UUID] UUIDString];
        VROCloudAnchorState state = VROCloudAnchorState::SUCCESS;
        
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(cloudAnchorId.UTF8String, state);
        });
    });
}

void VROARCloudAnchor::resolveCloudAnchor(const std::string& cloudAnchorId,
                                         std::function<void(std::shared_ptr<VROARCloudAnchor>, VROCloudAnchorState)> callback) {
    if (_provider == VROCloudAnchorProvider::NONE) {
        callback(nullptr, VROCloudAnchorState::ERROR_NOT_AUTHORIZED);
        return;
    }
    
    if (cloudAnchorId.empty()) {
        callback(nullptr, VROCloudAnchorState::ERROR_CLOUD_ID_NOT_FOUND);
        return;
    }
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // In real implementation, this would call ARCore cloud anchor resolution
        // GARSession *session = [GARSession sharedSession];
        // [session resolveCloudAnchor:cloudAnchorId completionHandler:callback];
        
        // Placeholder implementation
        std::shared_ptr<VROARCloudAnchor> anchor = std::make_shared<VROARCloudAnchor>(cloudAnchorId);
        anchor->setCloudAnchorState(VROCloudAnchorState::SUCCESS);
        
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(anchor, VROCloudAnchorState::SUCCESS);
        });
    });
}

void VROARCloudAnchor::setCloudAnchorProvider(VROCloudAnchorProvider provider) {
    _provider = provider;
}

VROCloudAnchorProvider VROARCloudAnchor::getCloudAnchorProvider() {
    return _provider;
}

void VROARCloudAnchor::updateFromARCore(void* garCloudAnchor) {
    _garCloudAnchor = garCloudAnchor;
    
    if (_garCloudAnchor) {
        // Extract data from ARCore cloud anchor
        // GARCloudAnchor *cloudAnchor = (__bridge GARCloudAnchor *)_garCloudAnchor;
        // Update transform and state from ARCore data
        _state = VROCloudAnchorState::SUCCESS;
    }
}

std::string VROARCloudAnchor::getStateDescription(VROCloudAnchorState state) {
    switch (state) {
        case VROCloudAnchorState::NONE:
            return "None";
        case VROCloudAnchorState::TASK_IN_PROGRESS:
            return "Task in progress";
        case VROCloudAnchorState::SUCCESS:
            return "Success";
        case VROCloudAnchorState::ERROR_INTERNAL:
            return "Internal error";
        case VROCloudAnchorState::ERROR_NOT_AUTHORIZED:
            return "Not authorized";
        case VROCloudAnchorState::ERROR_RESOURCE_EXHAUSTED:
            return "Resource exhausted";
        case VROCloudAnchorState::ERROR_HOSTING_DATASET_PROCESSING_FAILED:
            return "Hosting dataset processing failed";
        case VROCloudAnchorState::ERROR_CLOUD_ID_NOT_FOUND:
            return "Cloud ID not found";
        case VROCloudAnchorState::ERROR_RESOLVING_LOCALIZATION_NO_MATCH:
            return "Resolving localization no match";
        case VROCloudAnchorState::ERROR_RESOLVING_SDK_VERSION_TOO_OLD:
            return "Resolving SDK version too old";
        case VROCloudAnchorState::ERROR_RESOLVING_SDK_VERSION_TOO_NEW:
            return "Resolving SDK version too new";
        default:
            return "Unknown";
    }
}