//
//  VROCloudAnchorProviderReactVision.h
//  ViroKit (Android)
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  Proprietary and Confidential
//
//  Android bridge between VROARSessionARCore and the ReactVisionCCA C++ library.
//  Implements the same host/resolve interface as VROCloudAnchorProviderARCore
//  but routes operations through the ReactVision custom backend instead of
//  Google Cloud Anchors.
//
//  Wiring (in VROARSessionARCore.cpp):
//    #include "VROCloudAnchorProviderReactVision.h"
//
//    void VROARSessionARCore::setCloudAnchorProvider(VROCloudAnchorProvider p) {
//        ...
//        } else if (p == VROCloudAnchorProvider::ReactVision) {
//            _cloudAnchorProviderRV =
//                std::make_shared<VROCloudAnchorProviderReactVision>(
//                    shared_from_this(), "<api-key>", "<project-id>");
//            _synchronizer->addFrameListener(_cloudAnchorProviderRV);
//        }
//    }
//

#ifndef ANDROID_VROCLOUDANCHORPROVIDERREACTVISION_H
#define ANDROID_VROCLOUDANCHORPROVIDERREACTVISION_H

#include <string>
#include <memory>
#include <functional>
#include "VROFrameListener.h"

class VROARAnchor;
class VROARSessionARCore;
namespace ReactVisionCCA { class RVCCACloudAnchorProvider; }

/**
 * Manages host/resolve operations against the ReactVision cloud backend.
 *
 * Unlike VROCloudAnchorProviderARCore (which polls ARCore task state on every
 * frame), this class runs each operation on a background thread via
 * RVCCACloudAnchorProvider and delivers results via callbacks.  The
 * VROFrameListener interface is kept for consistency; onFrameWillRender is a
 * no-op unless periodic feature refreshing is added in the future.
 */
class VROCloudAnchorProviderReactVision : public VROFrameListener {
public:

    VROCloudAnchorProviderReactVision(
        std::shared_ptr<VROARSessionARCore> session,
        const std::string &apiKey,
        const std::string &projectId,
        const std::string &endpoint = "");

    virtual ~VROCloudAnchorProviderReactVision();

    /**
     * Host an anchor.  The current AR frame is captured from the session.
     *
     * @param anchor    Anchor to host (transform extracted at call time).
     * @param ttlDays   Lifetime in days (1-3650, ReactVision default: 365).
     * @param onSuccess Called with cloud anchor ID on success.
     * @param onFailure Called with human-readable error on failure.
     */
    void hostCloudAnchor(
        std::shared_ptr<VROARAnchor> anchor,
        int ttlDays,
        std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
        std::function<void(std::string error)>             onFailure);

    /**
     * Resolve a cloud anchor by ID.  The current AR frame is captured
     * from the session for localisation.
     *
     * @param cloudAnchorId ID returned by a previous hostCloudAnchor call.
     * @param onSuccess     Called with the resolved VROARAnchor.
     * @param onFailure     Called with human-readable error on failure.
     */
    void resolveCloudAnchor(
        std::string cloudAnchorId,
        std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
        std::function<void(std::string error)>             onFailure);

    // VROFrameListener
    void onFrameWillRender(const VRORenderContext &context) override {}

    /**
     * Called every frame by VROFrameSynchronizer.
     * Drives multi-frame host accumulation (Imp 6B) and resolve localization (Imp 1)
     * by forwarding fresh frame data into RVCCACloudAnchorProvider::updateWithFrame().
     */
    void onFrameDidRender(const VRORenderContext &context) override;

    /**
     * Returns the underlying RVCCACloudAnchorProvider for direct management API calls.
     * Returns nullptr when the ReactVisionCCA library is not available.
     */
    std::shared_ptr<ReactVisionCCA::RVCCACloudAnchorProvider> getProvider() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // ANDROID_VROCLOUDANCHORPROVIDERREACTVISION_H
