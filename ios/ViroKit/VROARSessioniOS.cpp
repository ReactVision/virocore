//
//  VROARSessioniOS.cpp
//  ViroKit
//
//  Created by Raj Advani on 6/6/17.
//  Copyright © 2017 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Availability.h"
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000
#include "VROARAnchor.h"
#include "VROARCameraiOS.h"
#include "VROARFrameiOS.h"
#include "VROARImageAnchor.h"
#include "VROARImageTargetiOS.h"
#include "VROARObjectAnchor.h"
#include "VROARObjectTargetiOS.h"
#include "VROARPlaneAnchor.h"
#include "VROARSessioniOS.h"
#include "VROBox.h"
#include "VROConvert.h"
#include "VRODriver.h"
#include "VROGeospatialAnchor.h"
#include "VROLog.h"
#include "VROPlatformUtil.h"
#include "VROPortal.h"
#include "VROProjector.h"
#include "VROScene.h"
#include "VROTexture.h"
#include "VROTextureSubstrate.h"
#include "VROVideoTextureCacheOpenGL.h"
#include "VROVisionModel.h"
#include "VROMonocularDepthEstimator.h"
#include <algorithm>

#import "VROCloudAnchorProviderARCore.h"
#import <simd/simd.h>

#pragma mark - Lifecycle and Initialization

VROARSessioniOS::VROARSessioniOS(VROTrackingType trackingType,
                                 VROWorldAlignment worldAlignment,
                                 std::shared_ptr<VRODriver> driver)
    : VROARSession(trackingType, worldAlignment),
      _sessionPaused(true),
      _monocularDepthEnabled(false),
      _preferMonocularDepth(false),
  _monocularDepthLoading(false),
      _driver(driver) {

  if (@available(iOS 11.0, *)) {
    _session = [[ARSession alloc] init];
  } else {
    pabort("ARKit not available on this OS");
  }
  _background = std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                             VROTextureInternalFormat::YCBCR);
  _videoTextureCache = std::dynamic_pointer_cast<VROVideoTextureCacheOpenGL>(
      driver->newVideoTextureCache());

  updateTrackingType(trackingType);
}

VROARSessioniOS::~VROARSessioniOS() {
    // Pause the ARKit session to stop processing
    if (_session && !_sessionPaused) {
        [_session pause];
        _sessionPaused = true;
    }

    // Clear the delegate to prevent callbacks during destruction
    if (_session) {
        _session.delegate = nil;
    }

    // Clear current frame
    _currentFrame.reset();

    // Clear all anchors
    _anchors.clear();
    _nativeAnchorMap.clear();

    // Clear background texture
    _background.reset();

    // Clear video texture cache (releases CVOpenGLESTextureCache)
    _videoTextureCache.reset();

    // Clear vision model
    _visionModel.reset();

    // Clear monocular depth estimator (releases CoreML model and resources)
    // This is critical to prevent memory leaks when AR session is destroyed
    _monocularDepthEnabled = false;
    if (_monocularDepthEstimator) {
        _monocularDepthEstimator.reset();
    }

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
    // Clear image detection resources
    if (_arKitImageDetectionSet) {
        [_arKitImageDetectionSet removeAllObjects];
        _arKitImageDetectionSet = nil;
    }
    _arKitReferenceImageMap.clear();
#endif

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
    // Clear object detection resources
    if (_arKitObjectDetectionSet) {
        [_arKitObjectDetectionSet removeAllObjects];
        _arKitObjectDetectionSet = nil;
    }
    _arKitReferenceObjectMap.clear();
#endif

    // Release ARKit objects
    _sessionConfiguration = nil;
    _delegateAR = nil;
    _session = nil;
}

void VROARSessioniOS::setTrackingType(VROTrackingType trackingType) {
  if (trackingType == _trackingType) {
    return;
  }

  updateTrackingType(trackingType);
  pause();
  run();
}

void VROARSessioniOS::updateTrackingType(VROTrackingType trackingType) {
  _trackingType = trackingType;

  if (getTrackingType() == VROTrackingType::DOF3) {
    NSLog(@"DOF3 tracking configuration");

    _sessionConfiguration = [[AROrientationTrackingConfiguration alloc] init];
    _sessionConfiguration.lightEstimationEnabled = YES;
  } else {
    NSLog(@"DOF6 tracking configuration");

    // Note that default anchor detection gets overwritten by VROARScene when
    // the session is injected into the scene (the scene will propagate whatever
    // anchor detection setting it has over to this session).
    ARWorldTrackingConfiguration *config =
        [[ARWorldTrackingConfiguration alloc] init];
    config.planeDetection = ARPlaneDetectionNone;
    config.lightEstimationEnabled = YES;
    switch (getWorldAlignment()) {
    case VROWorldAlignment::Camera:
      config.worldAlignment = ARWorldAlignmentCamera;
      break;
    case VROWorldAlignment::GravityAndHeading:
      config.worldAlignment = ARWorldAlignmentGravityAndHeading;
      break;
    case VROWorldAlignment::Gravity:
    default:
      config.worldAlignment = ARWorldAlignmentGravity;
      break;
    }

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
    if (@available(iOS 11.3, *)) {
      _arKitImageDetectionSet = [[NSMutableSet alloc] init];
      config.detectionImages = _arKitImageDetectionSet;
    }
#endif

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
    if (@available(iOS 12.0, *)) {
      _arKitObjectDetectionSet = [[NSMutableSet alloc] init];
      config.detectionObjects = _arKitObjectDetectionSet;
    }
#endif

    _sessionConfiguration = config;
  }
}

void VROARSessioniOS::run() {
  _sessionPaused = false;
  std::shared_ptr<VROARSessioniOS> shared = shared_from_this();
  _delegateAR = [[VROARKitSessionDelegate alloc] initWithSession:shared];
  _session.delegate = _delegateAR;

  [_session runWithConfiguration:_sessionConfiguration];
}

void VROARSessioniOS::pause() {
  _sessionPaused = true;
  [_session pause];
}

bool VROARSessioniOS::isReady() const {
  return getScene() != nullptr && _currentFrame.get() != nullptr;
}

void VROARSessioniOS::resetSession(bool resetTracking, bool removeAnchors) {
  if (_session && (resetTracking || removeAnchors)) {
    NSUInteger options =
        ((resetTracking ? ARSessionRunOptionResetTracking : 0) |
         (removeAnchors ? ARSessionRunOptionRemoveExistingAnchors : 0));
    [_session runWithConfiguration:_sessionConfiguration options:options];
  }
}

#pragma mark - Settings

void VROARSessioniOS::setScene(std::shared_ptr<VROScene> scene) {
  VROARSession::setScene(scene);
}

void VROARSessioniOS::setDelegate(
    std::shared_ptr<VROARSessionDelegate> delegate) {
  VROARSession::setDelegate(delegate);
  // When we add a new delegate, notify it of all the anchors we've found thus
  // far
  if (delegate) {
    for (auto it = _anchors.begin(); it != _anchors.end(); it++) {
      delegate->anchorWasDetected(*it);
    }
  }
}

void VROARSessioniOS::setAutofocus(bool enabled) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
  if (@available(iOS 11.3, *)) {
    if ([_sessionConfiguration
            isKindOfClass:[ARWorldTrackingConfiguration class]]) {
      ((ARWorldTrackingConfiguration *)_sessionConfiguration).autoFocusEnabled =
          enabled;
      [_session runWithConfiguration:_sessionConfiguration];
    }
  }
#endif
}

void VROARSessioniOS::setVideoQuality(VROVideoQuality quality) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
  if (@available(iOS 11.3, *)) {
    if ([_sessionConfiguration
            isKindOfClass:[ARWorldTrackingConfiguration class]]) {
      NSArray<ARVideoFormat *> *videoFormats =
          ARWorldTrackingConfiguration.supportedVideoFormats;
      int numberOfSupportedVideoFormats = (int)[videoFormats count];
      // Since iOS 12, ARWorldTrackingConfiguration.supportedVideoFormats
      // started returning 0 //// supportedVideoFormats here, for simulator
      // targets. In that case, we'll skip the following and run session with
      // default videoformat value
      if (numberOfSupportedVideoFormats > 0) {
        if (quality == VROVideoQuality::High) {
          ARVideoFormat *highestFormat;
          float high = 0;
          for (ARVideoFormat *format in videoFormats) {
            if (format.imageResolution.height > high) {
              high = format.imageResolution.height;
              highestFormat = format;
            }
          }
          ((ARWorldTrackingConfiguration *)_sessionConfiguration).videoFormat =
              highestFormat;
        } else {
          ARVideoFormat *lowestFormat;
          float low = CGFLOAT_MAX;
          for (ARVideoFormat *format in videoFormats) {
            if (format.imageResolution.height < low) {
              low = format.imageResolution.height;
              lowestFormat = format;
            }
          }
          ((ARWorldTrackingConfiguration *)_sessionConfiguration).videoFormat =
              lowestFormat;
        }
      }
    }
    [_session runWithConfiguration:_sessionConfiguration];
  }
#endif
}

void VROARSessioniOS::setViewport(VROViewport viewport) {
  _viewport = viewport;
}

void VROARSessioniOS::setOrientation(VROCameraOrientation orientation) {
  _orientation = orientation;
}

void VROARSessioniOS::setWorldOrigin(VROMatrix4f relativeTransform) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
  if (@available(iOS 11.3, *)) {
    if (_session) {
      [_session setWorldOrigin:VROConvert::toMatrixFloat4x4(relativeTransform)];
    }
  }
#endif
}

void VROARSessioniOS::setNumberOfTrackedImages(int numImages) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
  if (@available(iOS 12.0, *)) {
    if (_session && _sessionConfiguration) {
      if ([_sessionConfiguration
              isKindOfClass:[ARWorldTrackingConfiguration class]]) {
        ((ARWorldTrackingConfiguration *)_sessionConfiguration)
            .maximumNumberOfTrackedImages = numImages;
      } else if ([_sessionConfiguration
                     isKindOfClass:[ARImageTrackingConfiguration class]]) {
        ((ARImageTrackingConfiguration *)_sessionConfiguration)
            .maximumNumberOfTrackedImages = numImages;
      }
      [_session runWithConfiguration:_sessionConfiguration];
    }
  }
#endif
}

#pragma mark - Anchors

bool VROARSessioniOS::setAnchorDetection(std::set<VROAnchorDetection> types) {
  if (types.size() == 0) {
    if ([_sessionConfiguration
            isKindOfClass:[ARWorldTrackingConfiguration class]]) {
      ((ARWorldTrackingConfiguration *)_sessionConfiguration).planeDetection =
          ARPlaneDetectionNone;
    }
  } else {
    if ([_sessionConfiguration
            isKindOfClass:[ARWorldTrackingConfiguration class]]) {
      NSUInteger detectionTypes = ARPlaneDetectionNone; // default

      if (types.find(VROAnchorDetection::PlanesHorizontal) != types.end()) {
        detectionTypes = detectionTypes | ARPlaneDetectionHorizontal;
      }
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
      if (@available(iOS 11.3, *)) {
        if (types.find(VROAnchorDetection::PlanesVertical) != types.end()) {
          detectionTypes = detectionTypes | ARPlaneDetectionVertical;
        }
      }
#endif
      ((ARWorldTrackingConfiguration *)_sessionConfiguration).planeDetection =
          detectionTypes;
    }
  }

  // apply the configuration
  if (!_sessionPaused) {
    [_session runWithConfiguration:_sessionConfiguration];
  }
  return true;
}

void VROARSessioniOS::setCloudAnchorProvider(VROCloudAnchorProvider provider) {
  _cloudAnchorProvider = provider;

  if (provider == VROCloudAnchorProvider::ARCore) {
    // Initialize ARCore cloud anchor provider if not already done
    if (_cloudAnchorProviderARCore == nil) {
      if ([VROCloudAnchorProviderARCore isAvailable]) {
        _cloudAnchorProviderARCore = [[VROCloudAnchorProviderARCore alloc] init];
        if (_cloudAnchorProviderARCore) {
          pinfo("ARCore Cloud Anchor provider initialized successfully");
        } else {
          pwarn("Failed to initialize ARCore Cloud Anchor provider. Check GARAPIKey in Info.plist.");
        }
      } else {
        pwarn("ARCore SDK not available. Add ARCore/CloudAnchors pod to enable cloud anchors.");
      }
    }
  } else {
    // Clean up cloud anchor provider if switching to None
    if (_cloudAnchorProviderARCore != nil) {
      [_cloudAnchorProviderARCore cancelAllOperations];
      _cloudAnchorProviderARCore = nil;
    }
  }
}

void VROARSessioniOS::addAnchor(std::shared_ptr<VROARAnchor> anchor) {
  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (!delegate) {
    return;
  }

  delegate->anchorWasDetected(anchor);
  _anchors.push_back(anchor);
}

void VROARSessioniOS::removeAnchor(std::shared_ptr<VROARAnchor> anchor) {
  _anchors.erase(
      std::remove_if(_anchors.begin(), _anchors.end(),
                     [anchor](std::shared_ptr<VROARAnchor> candidate) {
                       return candidate == anchor;
                     }),
      _anchors.end());

  auto it = _nativeAnchorMap.find(anchor->getId());
  _nativeAnchorMap.erase(it);

  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (delegate) {
    delegate->anchorWasRemoved(anchor);
  }
}

void VROARSessioniOS::updateAnchor(std::shared_ptr<VROARAnchor> anchor) {
  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (delegate) {
    delegate->anchorWillUpdate(anchor);
  }
  anchor->updateNodeTransform();
  if (delegate) {
    delegate->anchorDidUpdate(anchor);
  }
}

void VROARSessioniOS::hostCloudAnchor(
    std::shared_ptr<VROARAnchor> anchor,
    int ttlDays,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string error)> onFailure) {
  if (_cloudAnchorProvider != VROCloudAnchorProvider::ARCore) {
    if (onFailure) {
      onFailure("Cloud anchor provider not configured. Set cloudAnchorProvider='arcore' to enable.");
    }
    return;
  }

  if (_cloudAnchorProviderARCore == nil) {
    if (onFailure) {
      onFailure("ARCore Cloud Anchor provider not initialized. Ensure ARCore SDK is available.");
    }
    return;
  }

  // Validate TTL: ARCore supports 1-365 days
  if (ttlDays < 1) {
    ttlDays = 1;
  } else if (ttlDays > 365) {
    ttlDays = 365;
  }

  // Find the native ARKit anchor for this VROARAnchor
  ARAnchor *nativeAnchor = nil;
  std::string anchorId = anchor->getId();

  // Search through the current ARKit anchors
  if (_currentFrame) {
    VROARFrameiOS *frameiOS = (VROARFrameiOS *)_currentFrame.get();
    ARFrame *arFrame = frameiOS->getARFrame();
    if (arFrame) {
      for (ARAnchor *arAnchor in arFrame.anchors) {
        if (std::string([[arAnchor.identifier UUIDString] UTF8String]) == anchorId) {
          nativeAnchor = arAnchor;
          break;
        }
      }
    }
  }

  if (!nativeAnchor) {
    if (onFailure) {
      onFailure("Could not find native ARKit anchor for hosting.");
    }
    return;
  }

  // Create weak reference to self for callback
  std::weak_ptr<VROARSessioniOS> weakSelf = shared_from_this();
  std::shared_ptr<VROARAnchor> anchorCopy = anchor;

  [_cloudAnchorProviderARCore hostAnchor:nativeAnchor
                                 ttlDays:ttlDays
                               onSuccess:^(NSString *cloudAnchorId, ARAnchor *resolvedAnchor) {
    auto strongSelf = weakSelf.lock();
    if (!strongSelf) return;

    // Update the anchor with the cloud anchor ID
    anchorCopy->setCloudAnchorId(std::string([cloudAnchorId UTF8String]));

    if (onSuccess) {
      onSuccess(anchorCopy);
    }
  }
                               onFailure:^(NSString *error) {
    if (onFailure) {
      onFailure(std::string([error UTF8String]));
    }
  }];
}

void VROARSessioniOS::resolveCloudAnchor(
    std::string cloudAnchorId,
    std::function<void(std::shared_ptr<VROARAnchor> anchor)> onSuccess,
    std::function<void(std::string error)> onFailure) {
  if (_cloudAnchorProvider != VROCloudAnchorProvider::ARCore) {
    if (onFailure) {
      onFailure("Cloud anchor provider not configured. Set cloudAnchorProvider='arcore' to enable.");
    }
    return;
  }

  if (_cloudAnchorProviderARCore == nil) {
    if (onFailure) {
      onFailure("ARCore Cloud Anchor provider not initialized. Ensure ARCore SDK is available.");
    }
    return;
  }

  NSString *cloudIdNS = [NSString stringWithUTF8String:cloudAnchorId.c_str()];
  std::weak_ptr<VROARSessioniOS> weakSelf = shared_from_this();
  std::string cloudIdCopy = cloudAnchorId;

  [_cloudAnchorProviderARCore resolveAnchor:cloudIdNS
                                  onSuccess:^(NSString *resolvedCloudId, ARAnchor *resolvedAnchor) {
    auto strongSelf = weakSelf.lock();
    if (!strongSelf) return;

    // Create a VROARAnchor from the resolved ARKit anchor
    std::shared_ptr<VROARAnchor> viroAnchor = std::make_shared<VROARAnchor>();
    viroAnchor->setId(std::string([[resolvedAnchor.identifier UUIDString] UTF8String]));
    viroAnchor->setCloudAnchorId(cloudIdCopy);
    viroAnchor->setTransform(VROConvert::toMatrix4f(resolvedAnchor.transform));

    // Add the anchor to the session
    strongSelf->addAnchor(viroAnchor);

    if (onSuccess) {
      onSuccess(viroAnchor);
    }
  }
                                  onFailure:^(NSString *error) {
    if (onFailure) {
      onFailure(std::string([error UTF8String]));
    }
  }];
}

#pragma mark - Frames

std::shared_ptr<VROTexture> VROARSessioniOS::getCameraBackgroundTexture() {
  return _background;
}

std::unique_ptr<VROARFrame> &VROARSessioniOS::updateFrame() {
  VROARFrameiOS *frameiOS = (VROARFrameiOS *)_currentFrame.get();

  /*
   Update the background image.
   */
  std::vector<std::unique_ptr<VROTextureSubstrate>> substrates =
      _videoTextureCache->createYCbCrTextureSubstrates(frameiOS->getImage());
  _background->setSubstrate(0, std::move(substrates[0]));
  _background->setSubstrate(1, std::move(substrates[1]));

  if (_visionModel) {
    _visionModel->update(frameiOS);
  }

  // Update monocular depth estimator if enabled and either LiDAR is unavailable
  // or monocular depth is explicitly preferred.
  if (_monocularDepthEnabled && _monocularDepthEstimator) {
    if (_preferMonocularDepth || !frameiOS->hasLiDARDepth()) {
      _monocularDepthEstimator->update(frameiOS);
    }
  }

  // Update cloud anchor provider to process pending operations
  if (_cloudAnchorProviderARCore != nil) {
    ARFrame *arFrame = frameiOS->getARFrame();
    if (arFrame) {
      [_cloudAnchorProviderARCore updateWithFrame:arFrame];
    }
  }

  return _currentFrame;
}

std::unique_ptr<VROARFrame> &VROARSessioniOS::getLastFrame() {
  return _currentFrame;
}

#pragma mark - Image Targets

void VROARSessioniOS::addARImageTarget(
    std::shared_ptr<VROARImageTarget> target) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
  // we'll get a warning for anything but: if(@available(...))
  if (@available(iOS 11.3, *)) {
    // we only support ARKit for now!
    std::shared_ptr<VROARImageTargetiOS> targetiOS =
        std::dynamic_pointer_cast<VROARImageTargetiOS>(target);
    if (targetiOS && getImageTrackingImpl() == VROImageTrackingImpl::ARKit &&
        getTrackingType() == VROTrackingType::DOF6) {
      // init the VROARImageTarget so it creates an ARReferenceImage
      targetiOS->initWithTrackingImpl(VROImageTrackingImpl::ARKit);
      ARReferenceImage *refImage = targetiOS->getARReferenceImage();

      // add the ARReferenceImage and the VROARImageTarget in a map
      _arKitReferenceImageMap[refImage] = target;

      // Add the ARReferenceImage to the set of images for detection, update the
      // config and "run" session. Note, we still need to set the config for the
      // ARSession to start detecting the new target (not just modifying the
      // set). Calling runConfiguration doesn't seem to be necessary in the
      // ARKit 1.5/iOS 11.3 preview, but it doesn't hurt and the "examples" that
      // they have do call it, so let's be safe.
      [_arKitImageDetectionSet addObject:refImage];
      ((ARWorldTrackingConfiguration *)_sessionConfiguration).detectionImages =
          _arKitImageDetectionSet;
      [_session runWithConfiguration:_sessionConfiguration];
    }
  } else {
    pwarn("[Viro] attempting to use ARKit 1.5 features while not on iOS 11.3+");
    return;
  }
#endif
}

void VROARSessioniOS::removeARImageTarget(
    std::shared_ptr<VROARImageTarget> target) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
  // we'll get a warning for anything but: if (@available(...))
  if (@available(iOS 11.3, *)) {
    std::shared_ptr<VROARImageTargetiOS> targetiOS =
        std::dynamic_pointer_cast<VROARImageTargetiOS>(target);
    if (targetiOS && getImageTrackingImpl() == VROImageTrackingImpl::ARKit &&
        getTrackingType() == VROTrackingType::DOF6) {
      ARReferenceImage *refImage = targetiOS->getARReferenceImage();
      if (refImage) {

        // call remove anchor (ARKit should do this IMHO).
        std::shared_ptr<VROARAnchor> anchor = target->getAnchor();
        if (anchor) {
          removeAnchor(anchor);
        }

        // delete the VROARImageTarget from _arKitReferenceImageMap
        for (auto it = _arKitReferenceImageMap.begin();
             it != _arKitReferenceImageMap.end();) {
          if (it->second == target) {
            it = _arKitReferenceImageMap.erase(it);
          } else {
            ++it;
          }
        }

        // delete the ARReferenceImage from the set of images to detect
        [_arKitImageDetectionSet removeObject:refImage];

        ((ARWorldTrackingConfiguration *)_sessionConfiguration)
            .detectionImages = _arKitImageDetectionSet;
        [_session runWithConfiguration:_sessionConfiguration];
      }
    }
  } else {
    pwarn("[Viro] attempting to use ARKit 1.5 features while not on iOS 11.3+");
    return;
  }
#endif
}

#pragma mark - Object Targets
void VROARSessioniOS::addARObjectTarget(
    std::shared_ptr<VROARObjectTarget> target) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
  if (@available(iOS 12.0, *)) {
    std::shared_ptr<VROARObjectTargetiOS> objectTarget =
        std::dynamic_pointer_cast<VROARObjectTargetiOS>(target);

    if (objectTarget && getTrackingType() == VROTrackingType::DOF6) {
      ARReferenceObject *refObject = objectTarget->getARReferenceObject();

      // add the ARReferenceObject & VROARObjectTarget to a map
      _arKitReferenceObjectMap[refObject] = target;

      // Add the ARReferenceObject to the set of objects for detection, update
      // the config and "run" session. Note, we still need to set the config for
      // the ARSession to start detecting the new target (not just modifying the
      // set).
      [_arKitObjectDetectionSet addObject:refObject];
      ((ARWorldTrackingConfiguration *)_sessionConfiguration).detectionObjects =
          _arKitObjectDetectionSet;
      [_session runWithConfiguration:_sessionConfiguration];
    }
  }
#endif
}

void VROARSessioniOS::removeARObjectTarget(
    std::shared_ptr<VROARObjectTarget> target) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
  if (@available(iOS 12.0, *)) {
    std::shared_ptr<VROARObjectTargetiOS> objectTarget =
        std::dynamic_pointer_cast<VROARObjectTargetiOS>(target);
    if (objectTarget && getTrackingType() == VROTrackingType::DOF6) {
      ARReferenceObject *refObject = objectTarget->getARReferenceObject();
      if (refObject) {

        // call remove anchor (ARKit should do this IMHO).
        std::shared_ptr<VROARAnchor> anchor = target->getAnchor();
        if (anchor) {
          removeAnchor(anchor);
        }

        // delete the VROARImageTarget from _arKitReferenceImageMap
        for (auto it = _arKitReferenceObjectMap.begin();
             it != _arKitReferenceObjectMap.end();) {
          if (it->second == target) {
            it = _arKitReferenceObjectMap.erase(it);
          } else {
            ++it;
          }
        }

        // delete the ARReferenceImage from the set of images to detect
        [_arKitObjectDetectionSet removeObject:refObject];

        ((ARWorldTrackingConfiguration *)_sessionConfiguration)
            .detectionObjects = _arKitObjectDetectionSet;
        [_session runWithConfiguration:_sessionConfiguration];
      }
    }
  }
#endif
}

#pragma mark - Occlusion Support

void VROARSessioniOS::setOcclusionMode(VROOcclusionMode mode) {
    VROARSession::setOcclusionMode(mode);

  bool lidarSupported = false;
  if (@available(iOS 14.0, *)) {
    lidarSupported = [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth];
  }

    // Enable scene depth in ARKit configuration for depth-based occlusion
    if (@available(iOS 14.0, *)) {
        if ([_sessionConfiguration isKindOfClass:[ARWorldTrackingConfiguration class]]) {
            ARWorldTrackingConfiguration *config = (ARWorldTrackingConfiguration *)_sessionConfiguration;

            if (mode == VROOcclusionMode::DepthBased) {
              // Enable scene depth if supported (requires LiDAR)
              if (lidarSupported) {
                config.frameSemantics = ARFrameSemanticSceneDepth;
              }
            } else if (mode == VROOcclusionMode::PeopleOnly) {
                // Enable person segmentation with depth
                if ([ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticPersonSegmentationWithDepth]) {
                    config.frameSemantics = ARFrameSemanticPersonSegmentationWithDepth;
                }
            } else {
                // Disable depth semantics
                config.frameSemantics = ARFrameSemanticNone;
            }

            if (!_sessionPaused) {
                [_session runWithConfiguration:_sessionConfiguration];
            }
        }
    }

        // Transparent monocular fallback for depth-based occlusion on non-LiDAR devices
        if (mode == VROOcclusionMode::DepthBased && !lidarSupported) {
          NSLog(@"Occlusion mode DepthBased set on non-LiDAR device - auto-enabling monocular depth");
          pinfo("Occlusion mode: Depth-based requested on non-LiDAR device, auto-enabling monocular depth");
          if (!_monocularDepthEnabled) {
            setMonocularDepthEnabled(true);
          }
        }

        // If user prefers monocular depth, ensure estimator is enabled so we can force mono
        if (mode == VROOcclusionMode::DepthBased && _preferMonocularDepth) {
          NSLog(@"Occlusion mode: User prefers monocular depth over LiDAR");
          pinfo("Occlusion mode: User prefers monocular depth, enabling estimator");
          if (!_monocularDepthEnabled) {
            setMonocularDepthEnabled(true);
          }
        }
}

bool VROARSessioniOS::isOcclusionSupported() const {
    // Check for LiDAR support first
    if (@available(iOS 14.0, *)) {
        if ([ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth]) {
            return true;
        }
    }

    // Fallback: check for monocular depth estimation support
    if (_monocularDepthEnabled && _monocularDepthEstimator) {
        return _monocularDepthEstimator->isAvailable();
    }

    return false;
}

bool VROARSessioniOS::isOcclusionModeSupported(VROOcclusionMode mode) const {
    if (mode == VROOcclusionMode::Disabled) {
        return true;
    }

    if (@available(iOS 14.0, *)) {
        if (mode == VROOcclusionMode::DepthBased) {
      if ([ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth]) {
        return true;
      }

      // Fallback: monocular depth estimation support (model + device)
      if (VROMonocularDepthEstimator::isSupported()) {
        NSBundle *frameworkBundle = [NSBundle bundleForClass:[VROARKitSessionDelegate class]];
        if (!frameworkBundle) {
          frameworkBundle = [NSBundle mainBundle];
        }

        NSString *bundledPath = [frameworkBundle pathForResource:@"DepthPro" ofType:@"mlmodelc"];
        if (!bundledPath) {
          bundledPath = [[NSBundle mainBundle] pathForResource:@"DepthPro" ofType:@"mlmodelc"];
        }

        return bundledPath != nil;
      }
      return false;
        } else if (mode == VROOcclusionMode::PeopleOnly) {
            return [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticPersonSegmentationWithDepth];
        }
    }
    return false;
}

#pragma mark - Monocular Depth Estimation

void VROARSessioniOS::setMonocularDepthEnabled(bool enabled) {
    _monocularDepthEnabled = enabled;
    NSLog(@"=== Monocular Depth: %s ===", enabled ? "ENABLED" : "DISABLED");
    pinfo("=== Monocular Depth: %s ===", enabled ? "ENABLED" : "DISABLED");
    NSLog(@"Monocular depth %s (loading=%d, estimator=%p)", enabled ? "enabled" : "disabled", _monocularDepthLoading, _monocularDepthEstimator.get());
    pinfo("Monocular depth %s (loading=%s, estimator=%s)",
      enabled ? "enabled" : "disabled",
      _monocularDepthLoading ? "true" : "false",
      _monocularDepthEstimator ? "initialized" : "null");

  if (enabled && !_monocularDepthEstimator && !_monocularDepthLoading) {
    _monocularDepthLoading = true;
    NSLog(@"Starting async DepthPro model load on background queue");
    pinfo("Starting async DepthPro model load on background queue");

    std::weak_ptr<VROARSessioniOS> weakSelf = shared_from_this();
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      auto strongSelf = weakSelf.lock();
      if (!strongSelf) {
        return;
      }

      // If monocular depth was disabled while loading, skip initialization
      if (!strongSelf->_monocularDepthEnabled) {
        strongSelf->_monocularDepthLoading = false;
        return;
      }

      // Try framework bundle first (model bundled in ViroKit)
      NSBundle *frameworkBundle = [NSBundle bundleForClass:[VROARKitSessionDelegate class]];

      // Fallback to main app bundle (for custom deployments)
      if (!frameworkBundle) {
        frameworkBundle = [NSBundle mainBundle];
      }

      NSString *bundledPath = [frameworkBundle pathForResource:@"DepthPro" ofType:@"mlmodelc"];

      // Fallback to main app bundle (for custom deployments)
      if (!bundledPath) {
        bundledPath = [[NSBundle mainBundle] pathForResource:@"DepthPro" ofType:@"mlmodelc"];
      }

      if (bundledPath) {
        NSLog(@"DepthPro model found at: %s", [bundledPath UTF8String]);
        pinfo("DepthPro model found at: %s", [bundledPath UTF8String]);
        strongSelf->initializeMonocularDepthEstimator(bundledPath);
      } else {
        NSLog(@"DepthPro.mlmodelc not found in bundle - monocular depth unavailable");
        pwarn("DepthPro.mlmodelc not found in bundle - monocular depth unavailable");
      }

      strongSelf->_monocularDepthLoading = false;
      NSLog(@"Monocular depth model load finished (estimator=%s)", strongSelf->_monocularDepthEstimator ? "initialized" : "failed");
      pinfo("Monocular depth model load finished (estimator=%s)",
          strongSelf->_monocularDepthEstimator ? "initialized" : "failed");
    });
  } else if (!enabled) {
    _monocularDepthLoading = false;
    NSLog(@"Disabling monocular depth, clearing estimator");
    pinfo("Disabling monocular depth, clearing estimator");
    // Disable - clear the estimator to save resources
    if (_monocularDepthEstimator) {
      _monocularDepthEstimator.reset();
    }
    }
}

bool VROARSessioniOS::isMonocularDepthEnabled() const {
    return _monocularDepthEnabled;
}

bool VROARSessioniOS::isMonocularDepthSupported() const {
    if (@available(iOS 14.0, *)) {
        return VROMonocularDepthEstimator::isSupported();
    }
    return false;
}

std::shared_ptr<VROMonocularDepthEstimator> VROARSessioniOS::getMonocularDepthEstimator() const {
    return _monocularDepthEstimator;
}

void VROARSessioniOS::setPreferMonocularDepth(bool prefer) {
    _preferMonocularDepth = prefer;
    NSLog(@"Prefer monocular depth over LiDAR: %s", prefer ? "YES" : "NO");
    pinfo("Prefer monocular depth over LiDAR: %s", prefer ? "YES" : "NO");

  if (prefer && !_monocularDepthEnabled) {
    // If occlusion mode is already DepthBased, we should enable monocular depth now
    // since the user explicitly requested preference for it
    if (getOcclusionMode() == VROOcclusionMode::DepthBased) {
        NSLog(@"[Monocular Depth] Preference set while DepthBased occlusion active - enabling estimator");
        pinfo("Preference set while DepthBased occlusion active, enabling monocular depth");
        setMonocularDepthEnabled(true);
    } else {
        NSLog(@"[Monocular Depth] Preference set but occlusion not DepthBased - waiting to enable");
        pinfo("Preference set but monocular depth not yet enabled (occlusion mode not DepthBased)");
    }
  }
}

bool VROARSessioniOS::isPreferMonocularDepth() const {
    return _preferMonocularDepth;
}

void VROARSessioniOS::initializeMonocularDepthEstimator(NSString *modelPath) {
    NSLog(@"Initializing monocular depth estimator with model: %s", [modelPath UTF8String]);
    pinfo("Initializing monocular depth estimator with model: %s", [modelPath UTF8String]);
    if (!_driver) {
        NSLog(@"ERROR: Cannot initialize depth estimator - no driver available");
        pwarn("Cannot initialize depth estimator - no driver available");
        return;
    }

    _monocularDepthEstimator = std::make_shared<VROMonocularDepthEstimator>(_driver);

    if (!_monocularDepthEstimator->initWithModel(modelPath)) {
        NSLog(@"ERROR: Failed to initialize monocular depth estimator");
        pwarn("Failed to initialize monocular depth estimator");
        _monocularDepthEstimator.reset();
        return;
    }

    NSLog(@"SUCCESS: Monocular depth estimator initialized and model loaded successfully");
    pinfo("Monocular depth estimator initialized and model loaded successfully");
}

#pragma mark - Internal Methods

std::shared_ptr<VROARAnchor>
VROARSessioniOS::getAnchorForNative(ARAnchor *anchor) {
  auto kv = _nativeAnchorMap.find(
      std::string([anchor.identifier.UUIDString UTF8String]));
  if (kv != _nativeAnchorMap.end()) {
    return kv->second;
  } else {
    return nullptr;
  }
}

void VROARSessioniOS::setVisionModel(
    std::shared_ptr<VROVisionModel> visionModel) {
  _visionModel = visionModel;
}

void VROARSessioniOS::setFrame(ARFrame *frame) {
  _currentFrame = std::unique_ptr<VROARFrame>(
      new VROARFrameiOS(frame, _viewport, _orientation, shared_from_this()));
}

void VROARSessioniOS::updateAnchorFromNative(
    std::shared_ptr<VROARAnchor> vAnchor, ARAnchor *anchor) {
  if ([anchor isKindOfClass:[ARPlaneAnchor class]]) {
    ARPlaneAnchor *planeAnchor = (ARPlaneAnchor *)anchor;

    std::shared_ptr<VROARPlaneAnchor> pAnchor =
        std::dynamic_pointer_cast<VROARPlaneAnchor>(vAnchor);

    // Get the anchor's world transform
    VROMatrix4f worldTransform = VROConvert::toMatrix4f(anchor.transform);

    // ARKit's planeAnchor.center is the plane center in the anchor's local
    // space To get world coordinates, transform it by the anchor's transform
    VROVector3f localCenter = VROConvert::toVector3f(planeAnchor.center);
    VROVector3f worldCenter = worldTransform.multiply(localCenter);

    // Store center in world coordinates (matching Android/Java API
    // expectations)
    pAnchor->setCenter(worldCenter);

    // Update extent directly from ARKit
    pAnchor->setExtent(VROConvert::toVector3f(planeAnchor.extent));

    // Update alignment
    if (planeAnchor.alignment == ARPlaneAnchorAlignmentHorizontal) {
      pAnchor->setAlignment(VROARPlaneAlignment::Horizontal);
    }
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
    else if (@available(iOS 11.3, *)) {
      if (planeAnchor.alignment == ARPlaneAnchorAlignmentVertical) {
        pAnchor->setAlignment(VROARPlaneAlignment::Vertical);
      }
    }
#endif

    // Update boundary vertices if available
    // ARKit provides boundary vertices in the plane's coordinate space,
    // already relative to the plane center. Use them directly.
    std::vector<VROVector3f> boundaryVertices;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
    if (@available(iOS 11.3, *)) {
      if (planeAnchor.geometry && planeAnchor.geometry.boundaryVertices &&
          planeAnchor.geometry.boundaryVertexCount > 0) {
        // Reserve space to avoid reallocations
        boundaryVertices.reserve(planeAnchor.geometry.boundaryVertexCount);

        // Apple documentation: "The boundary vertices are in the plane
        // coordinate space" Use them directly - they're already relative to the
        // plane center
        for (int i = 0; i < planeAnchor.geometry.boundaryVertexCount; i++) {
          vector_float3 vertex = planeAnchor.geometry.boundaryVertices[i];
          SCNVector3 vector3 = SCNVector3FromFloat3(vertex);

          // Use ARKit's vertices directly - no transformation needed
          boundaryVertices.push_back(
              VROVector3f(vector3.x, vector3.y, vector3.z));
        }
      }
    }
#endif
    pAnchor->setBoundaryVertices(std::move(boundaryVertices));

    // Extract full mesh geometry (iOS 11.3+)
    // This provides detailed tessellated surface beyond just boundary
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
    if (@available(iOS 11.3, *)) {
      if (planeAnchor.geometry) {
        ARPlaneGeometry *geometry = planeAnchor.geometry;

        // Extract mesh vertices (3D positions)
        std::vector<VROVector3f> meshVertices;
        if (geometry.vertices && geometry.vertexCount > 0) {
          meshVertices.reserve(geometry.vertexCount);
          for (int i = 0; i < geometry.vertexCount; i++) {
            vector_float3 vertex = geometry.vertices[i];
            meshVertices.push_back(VROVector3f(vertex.x, vertex.y, vertex.z));
          }
        }
        pAnchor->setMeshVertices(std::move(meshVertices));

        // Extract texture coordinates
        std::vector<VROVector2f> textureCoordinates;
        if (geometry.textureCoordinates &&
            geometry.textureCoordinateCount > 0) {
          textureCoordinates.reserve(geometry.textureCoordinateCount);
          for (int i = 0; i < geometry.textureCoordinateCount; i++) {
            vector_float2 uv = geometry.textureCoordinates[i];
            textureCoordinates.push_back(VROVector2f(uv.x, uv.y));
          }
        }
        pAnchor->setTextureCoordinates(std::move(textureCoordinates));

        // Extract triangle indices
        std::vector<int> triangleIndices;
        if (geometry.triangleIndices && geometry.triangleCount > 0) {
          triangleIndices.reserve(geometry.triangleCount * 3);
          for (int i = 0; i < geometry.triangleCount * 3; i++) {
            triangleIndices.push_back(geometry.triangleIndices[i]);
          }
        }
        pAnchor->setTriangleIndices(std::move(triangleIndices));
      }
    }
#endif

    // Extract plane classification (iOS 12+)
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
    if (@available(iOS 12.0, *)) {
      VROARPlaneClassification classification = VROARPlaneClassification::None;
      switch (planeAnchor.classification) {
      case ARPlaneClassificationWall:
        classification = VROARPlaneClassification::Wall;
        break;
      case ARPlaneClassificationFloor:
        classification = VROARPlaneClassification::Floor;
        break;
      case ARPlaneClassificationCeiling:
        classification = VROARPlaneClassification::Ceiling;
        break;
      case ARPlaneClassificationTable:
        classification = VROARPlaneClassification::Table;
        break;
      case ARPlaneClassificationSeat:
        classification = VROARPlaneClassification::Seat;
        break;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 130000
      case ARPlaneClassificationDoor:
        if (@available(iOS 13.0, *)) {
          classification = VROARPlaneClassification::Door;
        }
        break;
      case ARPlaneClassificationWindow:
        if (@available(iOS 13.0, *)) {
          classification = VROARPlaneClassification::Window;
        }
        break;
#endif
      case ARPlaneClassificationNone:
        classification = VROARPlaneClassification::Unknown;
        break;
      default:
        classification = VROARPlaneClassification::Unknown;
        break;
      }
      pAnchor->setClassification(classification);
    }
#endif

    // Record update for diagnostics
    pAnchor->recordUpdate(true);

#ifdef VRO_PLANE_PRECISION_DEBUG_LOGGING
    // PRECISION VALIDATION: Log comparison between ARKit raw data and ViroCore
    // processed data This helps validate that we're preserving native precision
    // WARNING: This logging happens on EVERY plane update and can severely
    // impact performance! Only enable for debugging precision issues.
    VROVector3f arKitLocalCenter = VROConvert::toVector3f(planeAnchor.center);
    VROVector3f arKitExtent = VROConvert::toVector3f(planeAnchor.extent);
    VROVector3f viroWorldCenter = pAnchor->getCenter();
    VROVector3f viroExtent = pAnchor->getExtent();
    VROVector3f transformPosition(worldTransform[12], worldTransform[13],
                                  worldTransform[14]);

    pinfo("ARKit Plane Precision Check:");
    pinfo("  ARKit planeAnchor.center (local): (%.6f, %.6f, %.6f)",
          arKitLocalCenter.x, arKitLocalCenter.y, arKitLocalCenter.z);
    pinfo("  ARKit anchor.transform position: (%.6f, %.6f, %.6f)",
          transformPosition.x, transformPosition.y, transformPosition.z);
    pinfo("  ViroCore center (world): (%.6f, %.6f, %.6f) [should match "
          "transform + local]",
          viroWorldCenter.x, viroWorldCenter.y, viroWorldCenter.z);
    pinfo("  ARKit extent: (%.6f, %.6f, %.6f)", arKitExtent.x, arKitExtent.y,
          arKitExtent.z);
    pinfo("  ViroCore extent: (%.6f, %.6f, %.6f)", viroExtent.x, viroExtent.y,
          viroExtent.z);
    pinfo("  Boundary vertices: %d", (int)boundaryVertices.size());
#endif
  }
  vAnchor->setTransform(VROConvert::toMatrix4f(anchor.transform));
}

void VROARSessioniOS::addAnchor(ARAnchor *anchor) {
  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (!delegate) {
    return;
  }

  std::shared_ptr<VROARAnchor> vAnchor;
  if ([anchor isKindOfClass:[ARPlaneAnchor class]]) {
    vAnchor = std::make_shared<VROARPlaneAnchor>();
  }
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110300
  // ignore the warning. The curious thing is that we don't even need the
  // @available() check...
  else if (@available(iOS 11.3, *)) {
    if ([anchor isKindOfClass:[ARImageAnchor class]]) {
      ARImageAnchor *imageAnchor = (ARImageAnchor *)anchor;
      auto it = _arKitReferenceImageMap.find(imageAnchor.referenceImage);
      if (it != _arKitReferenceImageMap.end()) {
        std::shared_ptr<VROARImageTarget> target = it->second;
        vAnchor = std::make_shared<VROARImageAnchor>(
            target, VROARImageTrackingMethod::Tracking);
        target->setAnchor(vAnchor);
      }
    }
  }
#endif
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 120000
  else if (@available(iOS 12.0, *)) {
    if ([anchor isKindOfClass:[ARImageAnchor class]]) {
      ARImageAnchor *imageAnchor = (ARImageAnchor *)anchor;
      auto it = _arKitReferenceImageMap.find(imageAnchor.referenceImage);
      if (it != _arKitReferenceImageMap.end()) {
        std::shared_ptr<VROARImageTarget> target = it->second;
        vAnchor = std::make_shared<VROARImageAnchor>(
            target, VROARImageTrackingMethod::Tracking);
        target->setAnchor(vAnchor);
      }
    }

    if ([anchor isKindOfClass:[ARObjectAnchor class]]) {
      ARObjectAnchor *objAnchor = (ARObjectAnchor *)anchor;
      auto it = _arKitReferenceObjectMap.find(objAnchor.referenceObject);
      if (it != _arKitReferenceObjectMap.end()) {
        std::shared_ptr<VROARObjectTarget> target = it->second;
        vAnchor = std::make_shared<VROARObjectAnchor>(target);
        target->setAnchor(vAnchor);
      }
    }
  }
#endif
  else {
    vAnchor = std::make_shared<VROARAnchor>();
  }
  vAnchor->setId(std::string([anchor.identifier.UUIDString UTF8String]));

  updateAnchorFromNative(vAnchor, anchor);

  addAnchor(vAnchor);
  _nativeAnchorMap[std::string([anchor.identifier.UUIDString UTF8String])] =
      vAnchor;
}

void VROARSessioniOS::updateAnchor(ARAnchor *anchor) {
  auto it = _nativeAnchorMap.find(
      std::string([anchor.identifier.UUIDString UTF8String]));
  if (it != _nativeAnchorMap.end()) {
    std::shared_ptr<VROARAnchor> vAnchor = it->second;

    // ATOMIC UPDATE: Sync anchor data from ARKit and immediately propagate
    // This ensures plane properties and transform are always consistent
    updateAnchorFromNative(vAnchor,
                           anchor); // Updates plane properties + transform
    updateAnchor(it->second);       // Immediately notifies delegates

    // Log diagnostics for plane anchors every 100 updates
    /*
    if ([anchor isKindOfClass:[ARPlaneAnchor class]]) {
      std::shared_ptr<VROARPlaneAnchor> pAnchor =
          std::dynamic_pointer_cast<VROARPlaneAnchor>(vAnchor);
      if (pAnchor && pAnchor->getUpdateCount() % 100 == 0) {
        pinfo("Plane %s: %u updates, extent: %.3f x %.3f, center: (%.3f, %.3f, "
              "%.3f)",
              std::string([anchor.identifier.UUIDString UTF8String]).c_str(),
              pAnchor->getUpdateCount(), pAnchor->getExtent().x,
              pAnchor->getExtent().z, pAnchor->getCenter().x,
              pAnchor->getCenter().y, pAnchor->getCenter().z);
      }
    }
      */
  } else {
    pinfo("Anchor %@ not found!", anchor.identifier);
  }
}

void VROARSessioniOS::removeAnchor(ARAnchor *anchor) {
  auto it = _nativeAnchorMap.find(
      std::string([anchor.identifier.UUIDString UTF8String]));
  if (it != _nativeAnchorMap.end()) {
    removeAnchor(it->second);
  }
}

#pragma mark - Geospatial API

void VROARSessioniOS::setGeospatialAnchorProvider(VROGeospatialAnchorProvider provider) {
  VROARSession::setGeospatialAnchorProvider(provider);

  if (provider == VROGeospatialAnchorProvider::ARCoreGeospatial) {
    // Initialize ARCore provider if not already done (same instance as cloud anchors)
    if (_cloudAnchorProviderARCore == nil) {
      if ([VROCloudAnchorProviderARCore isAvailable]) {
        _cloudAnchorProviderARCore = [[VROCloudAnchorProviderARCore alloc] init];
        if (_cloudAnchorProviderARCore) {
          pinfo("ARCore Geospatial provider initialized successfully");
        } else {
          pwarn("Failed to initialize ARCore Geospatial provider. Check GARAPIKey in Info.plist.");
        }
      } else {
        pwarn("ARCore SDK not available. Add ARCore/Geospatial pod to enable geospatial features.");
      }
    }

    // Enable geospatial mode
    if (_cloudAnchorProviderARCore) {
      [_cloudAnchorProviderARCore setGeospatialModeEnabled:YES];
    }
  }
}

bool VROARSessioniOS::isGeospatialModeSupported() const {
  if (_cloudAnchorProviderARCore) {
    return [_cloudAnchorProviderARCore isGeospatialModeSupported];
  }
  return [VROCloudAnchorProviderARCore isGeospatialAvailable];
}

void VROARSessioniOS::setGeospatialModeEnabled(bool enabled) {
  if (_cloudAnchorProviderARCore) {
    [_cloudAnchorProviderARCore setGeospatialModeEnabled:enabled];
  }
}

VROEarthTrackingState VROARSessioniOS::getEarthTrackingState() const {
  if (_cloudAnchorProviderARCore) {
    return [_cloudAnchorProviderARCore getEarthTrackingState];
  }
  return VROEarthTrackingState::Stopped;
}

VROGeospatialPose VROARSessioniOS::getCameraGeospatialPose() const {
  if (_cloudAnchorProviderARCore) {
    return [_cloudAnchorProviderARCore getCameraGeospatialPose];
  }
  return VROGeospatialPose();
}

void VROARSessioniOS::checkVPSAvailability(double latitude, double longitude,
                                           std::function<void(VROVPSAvailability)> callback) {
  if (_cloudAnchorProviderARCore) {
    [_cloudAnchorProviderARCore checkVPSAvailability:latitude
                                           longitude:longitude
                                            callback:^(VROVPSAvailability availability) {
      if (callback) {
        callback(availability);
      }
    }];
  } else if (callback) {
    callback(VROVPSAvailability::Unknown);
  }
}

void VROARSessioniOS::createGeospatialAnchor(double latitude, double longitude, double altitude,
                                             VROQuaternion quaternion,
                                             std::function<void(std::shared_ptr<VROGeospatialAnchor>)> onSuccess,
                                             std::function<void(std::string error)> onFailure) {
  if (!_cloudAnchorProviderARCore) {
    if (onFailure) {
      onFailure("Geospatial provider not initialized. Set geospatialAnchorProvider='arcore-geospatial'.");
    }
    return;
  }

  // Convert VROQuaternion to simd_quatf
  simd_quatf simdQuat = simd_quaternion(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W);

  [_cloudAnchorProviderARCore createGeospatialAnchor:latitude
                                           longitude:longitude
                                            altitude:altitude
                                          quaternion:simdQuat
                                           onSuccess:^(std::shared_ptr<VROGeospatialAnchor> anchor) {
    if (onSuccess) {
      onSuccess(anchor);
    }
  }
                                           onFailure:^(NSString *error) {
    if (onFailure) {
      onFailure(std::string([error UTF8String]));
    }
  }];
}

void VROARSessioniOS::createTerrainAnchor(double latitude, double longitude, double altitudeAboveTerrain,
                                          VROQuaternion quaternion,
                                          std::function<void(std::shared_ptr<VROGeospatialAnchor>)> onSuccess,
                                          std::function<void(std::string error)> onFailure) {
  if (!_cloudAnchorProviderARCore) {
    if (onFailure) {
      onFailure("Geospatial provider not initialized. Set geospatialAnchorProvider='arcore-geospatial'.");
    }
    return;
  }

  // Convert VROQuaternion to simd_quatf
  simd_quatf simdQuat = simd_quaternion(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W);

  [_cloudAnchorProviderARCore createTerrainAnchor:latitude
                                        longitude:longitude
                              altitudeAboveTerrain:altitudeAboveTerrain
                                       quaternion:simdQuat
                                        onSuccess:^(std::shared_ptr<VROGeospatialAnchor> anchor) {
    if (onSuccess) {
      onSuccess(anchor);
    }
  }
                                        onFailure:^(NSString *error) {
    if (onFailure) {
      onFailure(std::string([error UTF8String]));
    }
  }];
}

void VROARSessioniOS::createRooftopAnchor(double latitude, double longitude, double altitudeAboveRooftop,
                                          VROQuaternion quaternion,
                                          std::function<void(std::shared_ptr<VROGeospatialAnchor>)> onSuccess,
                                          std::function<void(std::string error)> onFailure) {
  if (!_cloudAnchorProviderARCore) {
    if (onFailure) {
      onFailure("Geospatial provider not initialized. Set geospatialAnchorProvider='arcore-geospatial'.");
    }
    return;
  }

  // Convert VROQuaternion to simd_quatf
  simd_quatf simdQuat = simd_quaternion(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W);

  [_cloudAnchorProviderARCore createRooftopAnchor:latitude
                                        longitude:longitude
                             altitudeAboveRooftop:altitudeAboveRooftop
                                       quaternion:simdQuat
                                        onSuccess:^(std::shared_ptr<VROGeospatialAnchor> anchor) {
    if (onSuccess) {
      onSuccess(anchor);
    }
  }
                                        onFailure:^(NSString *error) {
    if (onFailure) {
      onFailure(std::string([error UTF8String]));
    }
  }];
}

void VROARSessioniOS::removeGeospatialAnchor(std::shared_ptr<VROGeospatialAnchor> anchor) {
  if (_cloudAnchorProviderARCore && anchor) {
    NSString *anchorId = [NSString stringWithUTF8String:anchor->getId().c_str()];
    [_cloudAnchorProviderARCore removeGeospatialAnchor:anchorId];
  }
}

#pragma mark - Scene Semantics API

bool VROARSessioniOS::isSemanticModeSupported() const {
  // Scene Semantics on iOS requires ARCore SDK with Semantics extension
  // Initialize the provider if needed to check support
  if (_cloudAnchorProviderARCore == nil) {
    // Try to create provider temporarily to check support
    // Note: This is a const method, so we need a mutable cast
    VROARSessioniOS *mutableSelf = const_cast<VROARSessioniOS *>(this);
    mutableSelf->_cloudAnchorProviderARCore = [[VROCloudAnchorProviderARCore alloc] init];
    if (mutableSelf->_cloudAnchorProviderARCore) {
      pinfo("ARCore provider initialized for Scene Semantics support check");
    }
  }

  if (_cloudAnchorProviderARCore != nil) {
    return [_cloudAnchorProviderARCore isSemanticModeSupported];
  }
  return false;
}

void VROARSessioniOS::setSemanticModeEnabled(bool enabled) {
  _semanticModeEnabled = enabled;

  // Initialize ARCore provider if needed for semantics
  if (_cloudAnchorProviderARCore == nil && enabled) {
    _cloudAnchorProviderARCore = [[VROCloudAnchorProviderARCore alloc] init];
    if (_cloudAnchorProviderARCore) {
      pinfo("ARCore provider initialized for Scene Semantics");
    } else {
      pwarn("⚠️ Failed to initialize ARCore provider for Scene Semantics");
      pwarn("⚠️ Make sure GARAPIKey is set in Info.plist");
      _semanticModeEnabled = false;
      return;
    }
  }

  if (_cloudAnchorProviderARCore != nil) {
    // Check if semantic mode is supported before enabling
    if (enabled && !isSemanticModeSupported()) {
      pwarn("⚠️ Scene Semantics is not supported on this device, ignoring setSemanticModeEnabled(true)");
      _semanticModeEnabled = false;
      return;
    }

    [_cloudAnchorProviderARCore setSemanticModeEnabled:enabled];
    pinfo("Scene Semantics mode set to %s", enabled ? "ENABLED" : "DISABLED");
  } else if (enabled) {
    pwarn("⚠️ Scene Semantics requires ARCore SDK. Add ARCore/Semantics pod to your Podfile.");
    _semanticModeEnabled = false;
  }
}

float VROARSessioniOS::getSemanticLabelFraction(VROSemanticLabel label) const {
  if (_cloudAnchorProviderARCore != nil) {
    return [_cloudAnchorProviderARCore getSemanticLabelFraction:(NSInteger)label];
  }
  return 0.0f;
}

#pragma mark - VROARKitSessionDelegate

@interface VROARKitSessionDelegate ()

@property(readwrite, nonatomic) std::weak_ptr<VROARSessioniOS> session;

@end

@implementation VROARKitSessionDelegate

- (id)initWithSession:(std::shared_ptr<VROARSessioniOS>)session {
  self = [super init];
  if (self) {
    self.session = session;
  }
  return self;
}

- (void)session:(ARSession *)session didUpdateFrame:(ARFrame *)frame {
  std::shared_ptr<VROARSessioniOS> vSession = self.session.lock();
  if (vSession) {
    VROPlatformDispatchAsyncRenderer(
        [vSession, frame] { vSession->setFrame(frame); });
  }
}

- (void)session:(ARSession *)session
    didAddAnchors:(NSArray<ARAnchor *> *)anchors {
  std::shared_ptr<VROARSessioniOS> vSession = self.session.lock();
  if (vSession) {
    VROPlatformDispatchAsyncRenderer([vSession, anchors] {
      for (ARAnchor *anchor in anchors) {
        vSession->addAnchor(anchor);
      }
    });
  }
}

- (void)session:(ARSession *)session
    didUpdateAnchors:(NSArray<ARAnchor *> *)anchors {
  std::shared_ptr<VROARSessioniOS> vSession = self.session.lock();
  if (vSession) {
    VROPlatformDispatchAsyncRenderer([vSession, anchors] {
      for (ARAnchor *anchor in anchors) {
        vSession->updateAnchor(anchor);
      }
    });
  }
}

- (void)session:(ARSession *)session
    didRemoveAnchors:(NSArray<ARAnchor *> *)anchors {
  std::shared_ptr<VROARSessioniOS> vSession = self.session.lock();
  if (vSession) {
    VROPlatformDispatchAsyncRenderer([vSession, anchors] {
      for (ARAnchor *anchor in anchors) {
        vSession->removeAnchor(anchor);
      }
    });
  }
}

@end

#endif

