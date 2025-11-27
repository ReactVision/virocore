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
#include "VROLog.h"
#include "VROPlatformUtil.h"
#include "VROPortal.h"
#include "VROProjector.h"
#include "VROScene.h"
#include "VROTexture.h"
#include "VROTextureSubstrate.h"
#include "VROVideoTextureCacheOpenGL.h"
#include "VROVisionModel.h"
#include <algorithm>

#pragma mark - Lifecycle and Initialization

VROARSessioniOS::VROARSessioniOS(VROTrackingType trackingType,
                                 VROWorldAlignment worldAlignment,
                                 std::shared_ptr<VRODriver> driver)
    : VROARSession(trackingType, worldAlignment), _sessionPaused(true) {

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

VROARSessioniOS::~VROARSessioniOS() {}

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
  // TODO iOS ARCore Cloud Anchor implementation
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
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string error)> onFailure) {
  // Unsupproted
}
void VROARSessioniOS::resolveCloudAnchor(
    std::string anchorId,
    std::function<void(std::shared_ptr<VROARAnchor> anchor)> onSuccess,
    std::function<void(std::string error)> onFailure) {
  // Unsupported
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

    // Enable scene depth in ARKit configuration for depth-based occlusion
    if (@available(iOS 14.0, *)) {
        if ([_sessionConfiguration isKindOfClass:[ARWorldTrackingConfiguration class]]) {
            ARWorldTrackingConfiguration *config = (ARWorldTrackingConfiguration *)_sessionConfiguration;

            if (mode == VROOcclusionMode::DepthBased) {
                // Enable scene depth if supported (requires LiDAR)
                if ([ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth]) {
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
}

bool VROARSessioniOS::isOcclusionSupported() const {
    if (@available(iOS 14.0, *)) {
        // Check if scene depth is supported (requires LiDAR)
        return [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth];
    }
    return false;
}

bool VROARSessioniOS::isOcclusionModeSupported(VROOcclusionMode mode) const {
    if (mode == VROOcclusionMode::Disabled) {
        return true;
    }

    if (@available(iOS 14.0, *)) {
        if (mode == VROOcclusionMode::DepthBased) {
            return [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth];
        } else if (mode == VROOcclusionMode::PeopleOnly) {
            return [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticPersonSegmentationWithDepth];
        }
    }
    return false;
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
