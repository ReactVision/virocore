//
//  VROARSessionARCore.cpp
//  ViroKit
//
//  Created by Raj Advani on 9/27/17.
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

#include "VROARSessionARCore.h"
#include "VROARAnchor.h"
#include "VROGeospatialAnchor.h"
#include "VROARHitTestResult.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VRODriverOpenGL.h"
#include "VROARImageTargetAndroid.h"
#include "VROARPlaneAnchor.h"
#include "VROCameraTexture.h"
#include "VROCloudAnchorProviderARCore.h"
#include "VROCloudAnchorProviderReactVision.h"
#include "VRODriver.h"
#include "VROFrameSynchronizer.h"
#include "VROLog.h"
#include "VROPlatformUtil.h"
#include "VROScene.h"
#include "VROStringUtil.h"
#include "VROTexture.h"
#include "VROTextureSubstrateOpenGL.h"
#include <VROImageAndroid.h>
#include <algorithm>

static bool kDebugTracking = false;

// Minimum plane size filter to prevent small artifacts from being detected
// Planes smaller than this threshold will be ignored
static const float kMinPlaneExtent = 0.10f;  // 10cm minimum size in any dimension

VROARSessionARCore::VROARSessionARCore(std::shared_ptr<VRODriverOpenGL> driver)
    : VROARSession(VROTrackingType::DOF6, VROWorldAlignment::Gravity),
      _lightingMode(arcore::LightingMode::EnvironmentalHDR),
      _planeFindingMode(arcore::PlaneFindingMode::Horizontal),
      _updateMode(arcore::UpdateMode::Blocking),
      _cloudAnchorMode(arcore::CloudAnchorMode::Enabled),
      _focusMode(arcore::FocusMode::FIXED_FOCUS),
      _depthMode(arcore::DepthMode::Disabled),
      _semanticMode(arcore::SemanticMode::Disabled),
      _geospatialMode(arcore::GeospatialMode::Disabled),
      _cameraTextureId(0),
      _displayRotation(VROARDisplayRotation::R0), _rotatedImageDataLength(0),
      _rotatedImageData(nullptr),
      _driver(driver) {

  _session = nullptr;
  _frame = nullptr;
  _frameCount = 0;
}

void VROARSessionARCore::setARCoreSession(
    arcore::Session *session,
    std::shared_ptr<VROFrameSynchronizer> synchronizer) {
  _session = session;
  _synchronizer = synchronizer;

  if (getImageTrackingImpl() == VROImageTrackingImpl::ARCore) {
    _currentARCoreImageDatabase = _session->createAugmentedImageDatabase();
  }

  _cloudAnchorProvider =
      std::make_shared<VROCloudAnchorProviderARCore>(shared_from_this());
  _synchronizer->addFrameListener(_cloudAnchorProvider);
  _frame = _session->createFrame();
}

GLuint VROARSessionARCore::getCameraTextureId() const {
  return _cameraTextureId;
}

void VROARSessionARCore::initCameraTexture(
    std::shared_ptr<VRODriverOpenGL> driver) {
  // Generate the background texture
  glGenTextures(1, &_cameraTextureId);

  glBindTexture(GL_TEXTURE_EXTERNAL_OES, _cameraTextureId);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  std::unique_ptr<VROTextureSubstrate> substrate =
      std::unique_ptr<VROTextureSubstrateOpenGL>(new VROTextureSubstrateOpenGL(
          GL_TEXTURE_EXTERNAL_OES, _cameraTextureId, driver, true));
  _background = std::make_shared<VROTexture>(VROTextureType::TextureEGLImage,
                                             VROTextureInternalFormat::RGBA8,
                                             std::move(substrate));

  passert_msg(_session != nullptr,
              "ARCore must be installed before setting camera texture");
  _session->setCameraTextureName(_cameraTextureId);
}

VROARSessionARCore::~VROARSessionARCore() {
  if (_frame) {
    delete (_frame);
  }

  // Remove all anchors
  pinfo("Removing all anchors (%d) from session", (int)_anchors.size());

  std::vector<std::shared_ptr<VROARAnchorARCore>> anchorsToRemove = _anchors;
  for (std::shared_ptr<VROARAnchorARCore> anchor : anchorsToRemove) {
    removeAnchor(anchor);
    if (kDebugTracking) {
      pinfo("   Removed anchor %p on session destroy", anchor->getId().c_str());
    }
  }

  if (_session != nullptr) {
    pinfo("Destroying ARCore session");

    // Deleting the session could take a few seconds, so to prevent blocking the
    // main thread, they recommend pausing the session, then deleting on a
    // background thread!
    _session->pause();
    VROPlatformDispatchAsyncBackground([this] { delete (_session); });

    if (_currentARCoreImageDatabase != nullptr) {
      delete (_currentARCoreImageDatabase);
    }
  }

  if (_rotatedImageData != nullptr) {
    free(_rotatedImageData);
  }
}

#pragma mark - Lifecycle and Setup

void VROARSessionARCore::run() {
  if (_session != nullptr) {
    _session->resume();
    pinfo("AR session resumed");
  } else {
    pinfo("AR session not resumed: has not yet been configured");
  }
}

void VROARSessionARCore::pause() {
  if (_session != nullptr) {
    _session->pause();
    pinfo("AR session paused");
  } else {
    pinfo("AR session not paused: has not yet been configured");
  }
}

bool VROARSessionARCore::isReady() const {
  return getScene() != nullptr && _session != nullptr;
}

void VROARSessionARCore::resetSession(bool resetTracking, bool removeAnchors) {
  return; // no-op
}

bool VROARSessionARCore::setAnchorDetection(
    std::set<VROAnchorDetection> types) {
  std::set<VROAnchorDetection>::iterator it;

  bool planesHorizontal = false;
  bool planesVertical = false;

  for (it = types.begin(); it != types.end(); it++) {
    VROAnchorDetection type = *it;
    switch (type) {
    case VROAnchorDetection::PlanesHorizontal:
      planesHorizontal = true;
      break;
    case VROAnchorDetection::PlanesVertical:
      planesVertical = true;
      break;
    }
  }

  arcore::PlaneFindingMode newMode;
  if (planesHorizontal && planesVertical) {
    newMode = arcore::PlaneFindingMode::HorizontalAndVertical;
  } else if (planesHorizontal) {
    newMode = arcore::PlaneFindingMode::Horizontal;
  } else if (planesVertical) {
    newMode = arcore::PlaneFindingMode::Vertical;
  } else {
    newMode = arcore::PlaneFindingMode::Disabled;
  }

  // Avoid unnecessary reconfiguration if mode hasn't changed
  if (_planeFindingMode == newMode) {
    return true;
  }

  _planeFindingMode = newMode;
  return updateARCoreConfig();
}

void VROARSessionARCore::setReactVisionConfig(const std::string &apiKey,
                                              const std::string &projectId) {
  _rvApiKey    = apiKey;
  _rvProjectId = projectId;
  // Credentials supplied — activate the ReactVision cloud anchor provider.
  setCloudAnchorProvider(VROCloudAnchorProvider::ReactVision);
}

void VROARSessionARCore::setCloudAnchorProvider(
    VROCloudAnchorProvider provider) {

  if (provider == VROCloudAnchorProvider::ReactVision) {
    // ReactVision uses its own backend — keep ARCore cloud anchors disabled
    if (_cloudAnchorMode != arcore::CloudAnchorMode::Disabled) {
      _cloudAnchorMode = arcore::CloudAnchorMode::Disabled;
      updateARCoreConfig();
    }
    // Create provider if we have credentials
    if (!_cloudAnchorProviderRV && !_rvApiKey.empty() && !_rvProjectId.empty()) {
      _cloudAnchorProviderRV = std::make_shared<VROCloudAnchorProviderReactVision>(
          shared_from_this(), _rvApiKey, _rvProjectId);
    } else if (_rvApiKey.empty()) {
      pwarn("VROARSessionARCore: setReactVisionConfig() has not been called — "
            "ReactVision Cloud Anchors unavailable.");
    }
    return;
  }

  // Tear down RV provider when switching to ARCore or None
  _cloudAnchorProviderRV.reset();

  arcore::CloudAnchorMode newMode = (provider == VROCloudAnchorProvider::None)
    ? arcore::CloudAnchorMode::Disabled
    : arcore::CloudAnchorMode::Enabled;

  // Avoid unnecessary reconfiguration if mode hasn't changed
  if (_cloudAnchorMode == newMode) {
    return;
  }

  _cloudAnchorMode = newMode;
  updateARCoreConfig();
}

void VROARSessionARCore::setAutofocus(bool enabled) {
  arcore::FocusMode newMode = enabled ? arcore::FocusMode::AUTO_FOCUS : arcore::FocusMode::FIXED_FOCUS;

  // Avoid unnecessary reconfiguration if mode hasn't changed
  if (_focusMode == newMode) {
    return;
  }

  _focusMode = newMode;
  updateARCoreConfig();
}

bool VROARSessionARCore::isCameraAutoFocusEnabled() {
  if (_focusMode == arcore::FocusMode::AUTO_FOCUS) {
    return true;
  }
  return false;
}

void VROARSessionARCore::setDisplayGeometry(VROARDisplayRotation rotation,
                                            int width, int height) {
  _width = width;
  _height = height;
  _displayRotation = rotation;
  if (_session) {
    _session->setDisplayGeometry((int)rotation, width, height);
  }
}

bool VROARSessionARCore::configure(arcore::LightingMode lightingMode,
                                   arcore::PlaneFindingMode planeFindingMode,
                                   arcore::UpdateMode updateMode,
                                   arcore::CloudAnchorMode cloudAnchorMode,
                                   arcore::DepthMode depthMode,
                                   arcore::SemanticMode semanticMode,
                                   arcore::GeospatialMode geospatialMode) {
  _lightingMode = lightingMode;
  _planeFindingMode = planeFindingMode;
  _updateMode = updateMode;
  _cloudAnchorMode = cloudAnchorMode;
  _depthMode = depthMode;
  _semanticMode = semanticMode;
  _geospatialMode = geospatialMode;

  return updateARCoreConfig();
}

bool VROARSessionARCore::updateARCoreConfig() {
  passert_msg(_session != nullptr,
              "ARCore must be installed before configuring session");

  // Check if depth mode is supported on this device
  arcore::DepthMode effectiveDepthMode = _depthMode;
  if (_depthMode != arcore::DepthMode::Disabled) {
    if (!_session->isDepthModeSupported(_depthMode)) {
      pwarn("⚠️ Requested depth mode %d not supported on this device, falling back to DISABLED",
            (int)_depthMode);
      effectiveDepthMode = arcore::DepthMode::Disabled;
    }
  }

  // Check if semantic mode is supported on this device
  arcore::SemanticMode effectiveSemanticMode = _semanticMode;
  if (_semanticMode != arcore::SemanticMode::Disabled) {
    if (!_session->isSemanticModeSupported(_semanticMode)) {
      pwarn("⚠️ Requested semantic mode %d not supported on this device, falling back to DISABLED",
            (int)_semanticMode);
      effectiveSemanticMode = arcore::SemanticMode::Disabled;
    }
  }

  // Check if geospatial mode is supported on this device
  // This prevents the configuration loop when play-services-location is not linked
  arcore::GeospatialMode effectiveGeospatialMode = _geospatialMode;
  if (_geospatialMode != arcore::GeospatialMode::Disabled) {
    if (!_session->isGeospatialModeSupported(_geospatialMode)) {
      pwarn("⚠️ Requested geospatial mode not supported (missing play-services-location or API key?), falling back to DISABLED");
      effectiveGeospatialMode = arcore::GeospatialMode::Disabled;
    }
  }

  arcore::Config *config =
      _session->createConfig(_lightingMode, _planeFindingMode, _updateMode,
                             _cloudAnchorMode, _focusMode, effectiveDepthMode, effectiveSemanticMode, effectiveGeospatialMode);

  if (getImageTrackingImpl() == VROImageTrackingImpl::ARCore &&
      _currentARCoreImageDatabase) {
    config->setAugmentedImageDatabase(_currentARCoreImageDatabase);
  }

  // ARCore requires the session to be paused before calling configure()
  _session->pause();

  arcore::ConfigStatus status = _session->configure(config);
  delete (config);

  if (status == arcore::ConfigStatus::Success) {
    _session->resume();
    return true;
  } else {
    pwarn("Failed to configure AR session (status %d)", (int)status);
    _session->resume();
    return false;
  }
}

void VROARSessionARCore::setScene(std::shared_ptr<VROScene> scene) {
  VROARSession::setScene(scene);
}

void VROARSessionARCore::setDelegate(
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

void VROARSessionARCore::setViewport(VROViewport viewport) {
  _viewport = viewport;
}

void VROARSessionARCore::setOrientation(VROCameraOrientation orientation) {
  _orientation = orientation;
}

void VROARSessionARCore::setWorldOrigin(VROMatrix4f relativeTransform) {
  // no-op on Android
}

#pragma mark - AR Image Targets

void VROARSessionARCore::loadARImageDatabase(
    std::shared_ptr<VROARImageDatabase> arImageDatabase) {
  std::weak_ptr<VROARSessionARCore> w_arsession = shared_from_this();
  VROPlatformDispatchAsyncBackground([arImageDatabase, w_arsession] {
    std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
    if (arsession) {

      // load the image database from the given fileBuffer
      arcore::AugmentedImageDatabase *loadedDatabase =
          arsession->_session->createAugmentedImageDatabase(
              arImageDatabase->getFileData(), arImageDatabase->getLength());

      // add all the image targets to the database that were added through
      // addARImageTarget
      for (int i = 0; i < arsession->_imageTargets.size(); i++) {
        arsession->addTargetToDatabase(arsession->_imageTargets[i],
                                       loadedDatabase);
      }

      // update the ARCore config on the renderer thread
      VROPlatformDispatchAsyncRenderer([w_arsession, loadedDatabase] {
        std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
        if (arsession) {
          arsession->_currentARCoreImageDatabase = loadedDatabase;
          arsession->updateARCoreConfig();
        }
      });
    }
  });
}

void VROARSessionARCore::unloadARImageDatabase() {
  std::weak_ptr<VROARSessionARCore> w_arsession = shared_from_this();
  VROPlatformDispatchAsyncBackground([w_arsession] {
    std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
    if (arsession) {

      // create an empty image database
      arcore::AugmentedImageDatabase *database =
          arsession->_session->createAugmentedImageDatabase();

      // add all the image targets to the database that were added through
      // addARImageTarget
      for (int i = 0; i < arsession->_imageTargets.size(); i++) {
        arsession->addTargetToDatabase(arsession->_imageTargets[i], database);
      }

      // update the ARCore config on the renderer thread
      VROPlatformDispatchAsyncRenderer([w_arsession, database] {
        std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
        if (arsession) {
          arsession->_currentARCoreImageDatabase = database;
          arsession->updateARCoreConfig();
        }
      });
    }
  });
}

void VROARSessionARCore::addARImageTarget(
    std::shared_ptr<VROARImageTarget> target) {
  // on Android we always use Viro tracking implementation
  target->initWithTrackingImpl(getImageTrackingImpl());
  if (getImageTrackingImpl() == VROImageTrackingImpl::ARCore) {
    _imageTargets.push_back(target);
    std::weak_ptr<VROARSessionARCore> w_arsession = shared_from_this();
    VROPlatformDispatchAsyncBackground([target, w_arsession] {
      std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
      if (arsession) {
        arsession->addTargetToDatabase(target,
                                       arsession->_currentARCoreImageDatabase);
        // update the ARCore config on the renderer thread
        VROPlatformDispatchAsyncRenderer([w_arsession] {
          std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
          if (arsession) {
            arsession->updateARCoreConfig();
          }
        });
      }
    });
  }
}

void VROARSessionARCore::removeARImageTarget(
    std::shared_ptr<VROARImageTarget> target) {
  if (getImageTrackingImpl() == VROImageTrackingImpl::ARCore) {
    // First, we remove the target from the list of targets
    _imageTargets.erase(
        std::remove_if(_imageTargets.begin(), _imageTargets.end(),
                       [target](std::shared_ptr<VROARImageTarget> candidate) {
                         return candidate == target;
                       }),
        _imageTargets.end());

    arcore::AugmentedImageDatabase *oldDatabase = _currentARCoreImageDatabase;
    _currentARCoreImageDatabase = _session->createAugmentedImageDatabase();
    std::weak_ptr<VROARSessionARCore> w_arsession = shared_from_this();
    VROPlatformDispatchAsyncBackground([w_arsession, target] {
      std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
      if (arsession) {
        // Now add all the targets back into the database...
        for (int i = 0; i < arsession->_imageTargets.size(); i++) {
          arsession->addTargetToDatabase(
              arsession->_imageTargets[i],
              arsession->_currentARCoreImageDatabase);
        }

        // update the ARCore config on the renderer thread
        VROPlatformDispatchAsyncRenderer([w_arsession] {
          std::shared_ptr<VROARSessionARCore> arsession = w_arsession.lock();
          if (arsession) {
            // then "update" the config with the new target database.
            arsession->updateARCoreConfig();
          }
        });
      }
    });

    delete (oldDatabase);
  }
}

// Note: this function should be called on a background thread (as per guidance
// by ARCore for the
//       addImageWithPhysicalSize function).
void VROARSessionARCore::addTargetToDatabase(
    std::shared_ptr<VROARImageTarget> target,
    arcore::AugmentedImageDatabase *database) {
  std::shared_ptr<VROARImageTargetAndroid> targetAndroid =
      std::dynamic_pointer_cast<VROARImageTargetAndroid>(target);

  // a target w/o an image means it came from the database, so do nothing with
  // them!
  if (!targetAndroid->getImage()) {
    return;
  }

  std::shared_ptr<VROImageAndroid> imageAndroid =
      std::dynamic_pointer_cast<VROImageAndroid>(targetAndroid->getImage());

  size_t length;
  size_t stride;
  uint8_t *grayscaleImage = imageAndroid->getGrayscaleData(&length, &stride);
  int32_t outIndex;

  int width = imageAndroid->getWidth();
  int height = imageAndroid->getHeight();
  rotateImageForOrientation(&grayscaleImage, &width, &height, &stride,
                            target->getOrientation());

  arcore::AugmentedImageDatabaseStatus status = database->addImageWithPhysicalSize(
      targetAndroid->getId().c_str(), grayscaleImage, width, height,
      (int32_t)stride, target->getPhysicalWidth(), &outIndex);

  // Free that grayscaleImage now that we're done with it.
  free(grayscaleImage);
}

void VROARSessionARCore::rotateImageForOrientation(
    uint8_t **grayscaleImage, int *width, int *height, size_t *stride,
    VROImageOrientation orientation) {
  int length = (*width) * (*height);
  if (orientation == VROImageOrientation::Up) {
    *stride = (size_t)*width;
    uint8_t *rotatedImage = new uint8_t[length];
    memcpy(rotatedImage, *grayscaleImage, (size_t)length);
    *grayscaleImage = rotatedImage;
    return;
  } else if (orientation == VROImageOrientation::Down) {
    // if the image is "upside down" then just reverse it...
    *stride = (size_t)*width;
    uint8_t *rotatedImage = new uint8_t[length];
    int index;
    for (int i = 0; i < *height; i++) {
      for (int j = 0; j < *width; j++) {
        index = j + i * *width;
        rotatedImage[index] = (*grayscaleImage)[length - 1 - index];
      }
    }
    *grayscaleImage = rotatedImage;
  } else if (orientation == VROImageOrientation::Left) {
    // if the image is to the "Left" then rotate it CW by 90 degrees
    uint8_t *rotatedImage = new uint8_t[length];

    for (int i = 0; i < *width; i++) {
      for (int j = 0; j < *height; j++) {
        rotatedImage[j + i * *height] =
            (*grayscaleImage)[(*height - 1 - j) * *width + i];
      }
    }

    // since we rotated, swap the width and height.
    int tempWidth = *width;
    *width = *height;
    *height = tempWidth;

    // set the stride to the new width
    *stride = (size_t)*width;

    // set the grayscaleImage to the rotatedImage.
    *grayscaleImage = rotatedImage;

  } else if (orientation == VROImageOrientation::Right) {
    // if the image is to the "Right" then rotate it CCW by 90 degrees
    uint8_t *rotatedImage = new uint8_t[length];

    for (int i = 0; i < *width; i++) {
      for (int j = 0; j < *height; j++) {
        rotatedImage[j + i * *height] =
            (*grayscaleImage)[(*width) * (j + 1) - i - 1];
      }
    }

    // since we rotated, swap the width and height.
    int tempWidth = *width;
    *width = *height;
    *height = tempWidth;

    // set the stride to the new width
    *stride = (size_t)*width;

    // set the grayscaleImage to the rotatedImage.
    *grayscaleImage = rotatedImage;
  }
}

#pragma mark - Anchors

void VROARSessionARCore::addAnchor(std::shared_ptr<VROARAnchor> anchor) {
  std::shared_ptr<VROARAnchorARCore> vAnchor =
      std::dynamic_pointer_cast<VROARAnchorARCore>(anchor);
  passert(vAnchor);

  // Add the anchor under both its keys: the top-level anchor key and the
  // trackable key. The former keeps anchors we've created and attached to
  // trackables from being treated as "new" anchors in processUpdatedAnchors.
  if (vAnchor->getAnchorInternal()) {
    _nativeAnchorMap[VROStringUtil::toString64(
        vAnchor->getAnchorInternal()->getId())] = vAnchor;
  }
  _nativeAnchorMap[anchor->getId()] = vAnchor;

  if (kDebugTracking) {
    std::string nativeId =
        vAnchor->getAnchorInternal()
            ? VROStringUtil::toString64(vAnchor->getAnchorInternal()->getId())
            : "null";
    pinfo("Added new new anchor [%s -- %s]", nativeId.c_str(),
          anchor->getId().c_str());
  }

  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (delegate) {
    delegate->anchorWasDetected(anchor);
  }
  _anchors.push_back(vAnchor);
}

void VROARSessionARCore::removeAnchor(std::shared_ptr<VROARAnchor> anchor) {
  if (kDebugTracking) {
    pinfo("Removing anchor: anchor count %d, native anchor map size %d",
          (int)_anchors.size(), (int)_nativeAnchorMap.size());
  }
  _anchors.erase(
      std::remove_if(_anchors.begin(), _anchors.end(),
                     [anchor](std::shared_ptr<VROARAnchor> candidate) {
                       return candidate == anchor;
                     }),
      _anchors.end());

  for (auto it = _nativeAnchorMap.begin(); it != _nativeAnchorMap.end();) {
    if (it->second == anchor) {
      it = _nativeAnchorMap.erase(it);
    } else {
      ++it;
    }
  }
  if (kDebugTracking) {
    pinfo("   Anchor count after %d, native anchor map size after %d",
          (int)_anchors.size(), (int)_nativeAnchorMap.size());
  }

  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (delegate) {
    delegate->anchorWasRemoved(anchor);
  }
}

void VROARSessionARCore::updateAnchor(std::shared_ptr<VROARAnchor> anchor) {
  std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
  if (delegate) {
    delegate->anchorWillUpdate(anchor);
  }
  anchor->updateNodeTransform();
  if (delegate) {
    delegate->anchorDidUpdate(anchor);
  }
}

void VROARSessionARCore::hostCloudAnchor(
    std::shared_ptr<VROARAnchor> anchor,
    int ttlDays,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string error)> onFailure) {

  // ReactVision path — bypasses ARCore cloud anchors entirely
  if (_cloudAnchorProviderRV) {
    _cloudAnchorProviderRV->hostCloudAnchor(anchor, ttlDays, onSuccess, onFailure);
    return;
  }

  if (_cloudAnchorMode == arcore::CloudAnchorMode::Disabled) {
    pwarn("Cloud anchors are disabled, ignoring anchor host request");
    return;
  }
  _cloudAnchorProvider->hostCloudAnchor(anchor, ttlDays, onSuccess, onFailure);
}

void VROARSessionARCore::resolveCloudAnchor(
    std::string cloudAnchorId,
    std::function<void(std::shared_ptr<VROARAnchor> anchor)> onSuccess,
    std::function<void(std::string error)> onFailure) {

  // ReactVision path — bypasses ARCore cloud anchors entirely
  if (_cloudAnchorProviderRV) {
    _cloudAnchorProviderRV->resolveCloudAnchor(cloudAnchorId, onSuccess, onFailure);
    return;
  }

  if (_cloudAnchorMode == arcore::CloudAnchorMode::Disabled) {
    pwarn("Cloud anchors are disabled, ignoring anchor resolve request");
    return;
  }
  _cloudAnchorProvider->resolveCloudAnchor(cloudAnchorId, onSuccess, onFailure);
}

#pragma mark - AR Frames

std::shared_ptr<VROTexture> VROARSessionARCore::getCameraBackgroundTexture() {
  return _background;
}

std::unique_ptr<VROARFrame> &VROARSessionARCore::updateFrame() {
  _session->update(_frame);
  _currentFrame =
      std::make_unique<VROARFrameARCore>(_frame, _viewport, shared_from_this());

  VROARFrameARCore *arFrame = (VROARFrameARCore *)_currentFrame.get();
  arFrame->setDriver(_driver.lock());
  processUpdatedAnchors(arFrame);
  updateDepthTexture();

  return _currentFrame;
}

std::unique_ptr<VROARFrame> &VROARSessionARCore::getLastFrame() {
  return _currentFrame;
}

#pragma mark - Internal Methods

std::shared_ptr<VROARAnchor>
VROARSessionARCore::getAnchorWithId(std::string anchorId) {
  auto it = _nativeAnchorMap.find(anchorId);
  if (it != _nativeAnchorMap.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

std::shared_ptr<VROARAnchor>
VROARSessionARCore::getAnchorForNative(arcore::Anchor *anchor) {
  std::string key = VROStringUtil::toString(anchor->getHashCode());
  auto it = _nativeAnchorMap.find(key);
  if (it != _nativeAnchorMap.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

/*
 This method does most of the ARCore processing. ARCore consists of two
 concepts: trackable and anchor. Trackables are detected real-world objects,
 like horizontal and vertical planes, or image targets. Anchors are virtual
 objects that are attached to the real world, either relative to a trackable,
 relative to an AR hit result, or relative to an arbitrary position.

 Unlike ARCore, Viro (and ARKit) merge these concepts together: trackables *are*
 anchors. In order to bridge this conceptual difference with ARCore, this method
 will create one ARCore anchor for every ARCore trackable found. It will attach
 that anchor to the trackable with the trackable's center pose.

 We then create a Viro object to correspond to each of these: a
 VROARAnchorARCore to correspond to the anchor we created for the trackable, and
 another VROARAnchor subclass to correspond to the trackable itself. For
 example, for planes:

 1. ARCore detects a new arcore::Plane
 2. We create an arcore::Anchor attached to the plane (via
 arcore::Plane->acquireAnchor)
 3. We create Viro object VROARAnchorARCore to correspond to the arcore::Anchor
 4. We create Viro object VROARAnchorPlane to correspond to the arcore::Plane
 5. We associate the VROARAnchorARCore to the VROARAnchorPlane via
 VRORAAnchorARCore->setTrackable()
 6. We place the VROARAnchorARCore in the _nativeAnchorMap and the _anchors list

 One point of confusion is that both anchors and trackables have their own
 transformation matrix. We use the anchor transformation matrix when determining
 how to place the ARNodes that we generate for each created Anchor. This is for
 compatibility with cloud anchors: the devices receiving the content will only
 have the anchor transformation.

 All anchors found here are placed in the _nativeAnchorMap. We only place the
 top-level anchor in the map. We do not place the trackable anchors themselves
 in the map. For each type of anchor we use a different key:

 1. For anchors without a trackable, we key by the anchor's ID.
 2. For plane trackables, we key by the anchor's ID *and* by the trackable's
 pointer address. Inserting keys for both the anchor and the trackable ensures
 that we don't treat the anchor we've created for the trackable as a brand new
 anchor during the next processUpdatedAnchors call.
 3. For image trackables, key by the anchor's ID *and* the image's name. Note
 that keying by the image's name has the effect of ensuring we only recognize
 *one* image of a type at a time.

 Finally, all anchors found are also placed in the _anchors list. As with the
 _nativeAnchorMap, we only place top-level anchors here (not the trackable
 anchors).
 */
void VROARSessionARCore::processUpdatedAnchors(VROARFrameARCore *frameAR) {
  std::shared_ptr<VROARSessionARCore> session = shared_from_this();
  arcore::Frame *frame = frameAR->getFrameInternal();

  arcore::AnchorList *anchorList = _session->createAnchorList();
  frame->getUpdatedAnchors(anchorList);
  int anchorsSize = anchorList->size();

  // Find all new and updated anchors, update/create new ones and notify this
  // class. The anchors in this list are *both* those that are tied to
  // trackables (managed anchors) and those that were created at arbitrary world
  // positions or in response to hit tests (manual anchors). However, we only
  // process manual anchors here. Anchors with trackables are processed
  // afterward as the trackables themselves are updated.
  for (int i = 0; i < anchorsSize; i++) {
    std::shared_ptr<arcore::Anchor> anchor =
        std::shared_ptr<arcore::Anchor>(anchorList->acquireItem(i));
    std::string key = VROStringUtil::toString64(anchor->getId());
    auto it = _nativeAnchorMap.find(key);

    // Previously found anchor: update
    if (it != _nativeAnchorMap.end()) {
      std::shared_ptr<VROARAnchorARCore> vAnchor = it->second;

      // Only update manual anchors. If the anchor has a trackable, do not
      // process it (it will be processed with its associated trackable below)
      if (!vAnchor->isManaged()) {
        passert(anchor->getId() == vAnchor->getAnchorInternal()->getId());
        vAnchor->sync();
        updateAnchor(vAnchor);
      } else {
        // If the anchor is managed by a VROGeospatialAnchor (which is not an ARCore trackable),
        // we need to manually sync it here.
        std::shared_ptr<VROGeospatialAnchor> geoAnchor = std::dynamic_pointer_cast<VROGeospatialAnchor>(vAnchor->getTrackable());
        if (geoAnchor) {
            vAnchor->sync();
            geoAnchor->updateFromGeospatialTransform(vAnchor->getTransform());
            updateAnchor(geoAnchor);
        }
      }

      // New or removed anchor.
    } else {
      arcore::TrackingState trackingState = anchor->getTrackingState();

      // We have a new anchor detected by ARCore that isn't tied to a trackable.
      // ARCore will never magically create an anchor that isn't tied to a
      // trackable, except when acquiring new cloud anchors to host.
      //
      // Note we ignore anchors that are NotTracking, as this is just ARCore
      // telling us that a managed anchor has been removed in the last frame.
      if (trackingState != arcore::TrackingState::NotTracking) {
        pinfo("Detected new anchor with no association (may be cloud anchor) "
              "[%p]",
              key.c_str());
      }
    }
  }

  arcore::TrackableList *planeList = _session->createTrackableList();
  frame->getUpdatedTrackables(planeList, arcore::TrackableType::Plane);
  int planeSize = planeList->size();

  // Find all new and updated planes and process them. For new planes we will
  // create a corresponding anchor. For updated planes we will update the planes
  // and the anchor. Finally, we remove subsumed planes.
  for (int i = 0; i < planeSize; i++) {
    arcore::Trackable *trackable = planeList->acquireItem(i);
    arcore::Plane *plane = (arcore::Plane *)trackable;
    arcore::Plane *subsumingPlane = plane->acquireSubsumedBy();

    arcore::TrackingState state = trackable->getTrackingState();
    bool currentPlaneIsTracked = (state == arcore::TrackingState::Tracking);

    // ARCore doesn't use ID for planes, but rather they simply return the same
    // object, so the hashcodes (which in this case are pointer addresses)
    // should be reliable
    std::string key = getKeyForTrackable(plane);

    // The plane was *NOT* subsumed by a new plane and is still tracking:
    // either add or update it
    if (subsumingPlane == NULL && currentPlaneIsTracked) {
      auto it = _nativeAnchorMap.find(key);

      // The plane is old: update it
      if (it != _nativeAnchorMap.end()) {
        std::shared_ptr<VROARAnchorARCore> vAnchor = it->second;

        if (vAnchor) {
          std::shared_ptr<VROARPlaneAnchor> vPlane =
              std::dynamic_pointer_cast<VROARPlaneAnchor>(
                  vAnchor->getTrackable());

          // ATOMIC UPDATE: Sync plane data and anchor transform together
          // to ensure they're never out of sync
          syncPlaneWithARCore(vPlane, plane); // Updates plane properties
          vAnchor->sync(); // Updates anchor transform to match ARCore anchor

          // Immediately propagate to application - no delay
          // This ensures the app sees consistent plane data + transform
          updateAnchor(vAnchor);
        } else {
          pwarn("Anchor processing error: expected to find a plane");
        }

        // The plane is new: add it
      } else {
        pinfo("Detected new anchor tied to plane");

        std::shared_ptr<VROARPlaneAnchor> vPlane =
            std::make_shared<VROARPlaneAnchor>();
        syncPlaneWithARCore(vPlane, plane);

        // Filter out small planes (likely artifacts/noise)
        VROVector3f extent = vPlane->getExtent();
        float maxExtent = std::max(extent.x, extent.z);

        if (maxExtent < kMinPlaneExtent) {
          pinfo("Filtering out small plane (extent: %.3f x %.3f, threshold: %.3f)",
                extent.x, extent.z, kMinPlaneExtent);
          delete (trackable);
          continue;
        }

        // PURE ANDROID FIX: Do NOT create an arcore::Anchor for the plane.
        // Instead, we use the plane's trackable pose directly.
        // This prevents "fighting" between the anchor pose and the plane center
        // pose.
        std::shared_ptr<VROARAnchorARCore> vAnchor =
            std::make_shared<VROARAnchorARCore>(key, nullptr, vPlane, session);

        // Explicitly set the transform from the plane (since sync() won't do it
        // for null anchors)
        vAnchor->setTransform(vPlane->getTransform());

        addAnchor(vAnchor);
      }

      // The plane has been subsumed or is no longer tracked: remove it
    } else {
      auto it = _nativeAnchorMap.find(key);
      if (it != _nativeAnchorMap.end()) {
        if (subsumingPlane) {
          pinfo("Plane %s subsumed: removing", key.c_str());
        } else {
          pinfo("Plane %s no longer tracked: removing", key.c_str());
        }

        std::shared_ptr<VROARAnchorARCore> vAnchor = it->second;
        if (vAnchor) {
          removeAnchor(vAnchor);
        }
      }
      delete (subsumingPlane);
    }
    delete (trackable);
  }

  // Process updated/new images if the tracking implementation is ARCore. This
  // process is virtually identical to how we handle planes above.
  if (getImageTrackingImpl() == VROImageTrackingImpl::ARCore) {

    arcore::TrackableList *imageList = _session->createTrackableList();
    frame->getUpdatedTrackables(imageList, arcore::TrackableType::Image);
    int imageSize = imageList->size();
    for (int i = 0; i < imageSize; i++) {
      arcore::Trackable *trackable = imageList->acquireItem(i);
      arcore::AugmentedImage *image = (arcore::AugmentedImage *)trackable;

      // The name of the image is used for image anchors. This enforces the
      // condition that we only detect each image once
      std::string key = getKeyForTrackable(image);

      arcore::TrackingMethod arTrackingMethod = image->getTrackingMethod();
      VROARImageTrackingMethod trackingMethod =
          VROARImageTrackingMethod::NotTracking;
      if (arTrackingMethod == arcore::TrackingMethod::Tracking) {
        trackingMethod = VROARImageTrackingMethod::Tracking;
      } else if (arTrackingMethod == arcore::TrackingMethod::LastKnownPose) {
        trackingMethod = VROARImageTrackingMethod::LastKnownPose;
      }

      bool imageIsTracked =
          (trackable->getTrackingState() == arcore::TrackingState::Tracking);
      if (imageIsTracked) {
        auto it = _nativeAnchorMap.find(key);

        // Old image tracking target: update it
        if (it != _nativeAnchorMap.end()) {
          std::shared_ptr<VROARAnchorARCore> vAnchor = it->second;
          std::shared_ptr<VROARImageAnchor> imageAnchor =
              std::dynamic_pointer_cast<VROARImageAnchor>(
                  vAnchor->getTrackable());

          if (vAnchor) {
            imageAnchor->setTrackingMethod(trackingMethod);
            syncImageAnchorWithARCore(imageAnchor, image);
            vAnchor->sync();
            updateAnchor(vAnchor);
          } else {
            pwarn("Anchor processing error: expected to find an image anchor");
          }

          // New image tracking target: add it
        } else {
          std::shared_ptr<VROARImageTargetAndroid> target;

          bool haveFoundTarget = false;
          // first, loop over all targets to see if the target matches the found
          // ImageAnchor
          for (int j = 0; j < _imageTargets.size(); j++) {
            target = std::dynamic_pointer_cast<VROARImageTargetAndroid>(
                _imageTargets[j]);
            if (key == target->getId()) {
              haveFoundTarget = true;
              // break out of the loop since we found a target id that matches
              // the key
              break;
            }
          }
          // No target found means that the AR system found an ImageAnchor w/o
          // us knowing the target, this probably means that it was loaded from
          // an ARImageDatabase, so lets create a new target
          if (!haveFoundTarget) {
            target = std::make_shared<VROARImageTargetAndroid>(key);
          }

          std::shared_ptr<VROARImageAnchor> vImage =
              std::make_shared<VROARImageAnchor>(target, trackingMethod);
          syncImageAnchorWithARCore(vImage, image);

          // Create a new anchor to correspond with the found image
          arcore::Pose *pose = _session->createPose();
          image->getCenterPose(pose);

          std::shared_ptr<arcore::Anchor> anchor =
              std::shared_ptr<arcore::Anchor>(image->acquireAnchor(pose));
          if (anchor) {
            std::shared_ptr<VROARAnchorARCore> vAnchor =
                std::make_shared<VROARAnchorARCore>(key, anchor, vImage,
                                                    session);
            vAnchor->sync();
            addAnchor(vAnchor);
          } else {
            pinfo("Failed to create anchor for trackable image target: will "
                  "try again later");
          }
          delete (pose);
        }
        // The image is no longer being tracked: remove it
      } else {
        auto it = _nativeAnchorMap.find(key);
        if (it != _nativeAnchorMap.end()) {
          pinfo("Image target [%s] has lost tracking, removing", key.c_str());

          std::shared_ptr<VROARAnchorARCore> vAnchor = it->second;
          if (vAnchor) {
            removeAnchor(vAnchor);
          }
        }
      }
    }
    delete (imageList);
  }

  delete (anchorList);
  delete (planeList);
}

std::string
VROARSessionARCore::getKeyForTrackable(arcore::Trackable *trackable) {
  arcore::TrackableType type = trackable->getType();
  if (type == arcore::TrackableType::Plane) {
    arcore::Plane *plane = (arcore::Plane *)trackable;
    return VROStringUtil::toString(plane->getHashCode());
  } else if (type == arcore::TrackableType::Image) {
    arcore::AugmentedImage *image = (arcore::AugmentedImage *)trackable;
    return std::string(image->getName());
  } else {
    pwarn("Attempting to get key for invalid trackable type");
    return "";
  }
}

std::shared_ptr<VROARAnchorARCore>
VROARSessionARCore::getAnchorForTrackable(arcore::Trackable *trackable) {
  std::string key = getKeyForTrackable(trackable);
  if (key.empty()) {
    return nullptr;
  }
  auto it = _nativeAnchorMap.find(key);
  if (it == _nativeAnchorMap.end()) {
    return nullptr;
  }
  return it->second;
}

void VROARSessionARCore::syncPlaneWithARCore(
    std::shared_ptr<VROARPlaneAnchor> plane, arcore::Plane *planeAR) {
  // Get the plane's center pose directly from ARCore
  // In ARCore, the anchor is created AT the plane's center, so we use
  // the center pose as the anchor transform directly
  arcore::Pose *centerPose = _session->createPose();
  planeAR->getCenterPose(centerPose);

  float transformMtx[16];
  centerPose->toMatrix(transformMtx);

  VROMatrix4f transform(transformMtx);

  // CRITICAL: Set transform on VROARPlaneAnchor because that's what the app
  // uses! In Android's dual-anchor architecture, the app gets the
  // VROARPlaneAnchor via getAnchorForTrackable(), so it needs the transform. We
  // use the plane's current center pose directly, which updates as the plane
  // grows.
  plane->setTransform(transform);

  // Fix for Regression: Set center to (0,0,0) (Local Coordinates)
  // Since we just set the anchor's transform to be the plane's center pose
  // (above), the plane's center relative to the anchor is the origin.
  // Previously, this was set to 'worldCenter', causing a double-transformation
  // when the app added WorldPosition (Anchor) + WorldPosition (Center).
  plane->setCenter(VROVector3f(0, 0, 0));

  delete (centerPose);

  // Update alignment directly from ARCore
  switch (planeAR->getPlaneType()) {
  case arcore::PlaneType::HorizontalUpward:
    plane->setAlignment(VROARPlaneAlignment::HorizontalUpward);
    break;
  case arcore::PlaneType::HorizontalDownward:
    plane->setAlignment(VROARPlaneAlignment::HorizontalDownward);
    break;
  case arcore::PlaneType::Vertical:
    plane->setAlignment(VROARPlaneAlignment::Vertical);
    break;
  default:
    plane->setAlignment(VROARPlaneAlignment::Horizontal);
  }

  // Update extent directly from ARCore
  float extentX = planeAR->getExtentX();
  float extentZ = planeAR->getExtentZ();
  plane->setExtent(VROVector3f(extentX, 0, extentZ));

  // Update boundary vertices directly from ARCore
  // ARCore provides polygon vertices in plane-local space (relative to center)
  std::vector<VROVector3f> boundaryVertices;
  float *polygonArray = planeAR->getPolygon();
  int polygonArraySize = planeAR->getPolygonSize();

  if (polygonArraySize > 0) {
    // Reserve space to avoid reallocations
    boundaryVertices.reserve(polygonArraySize / 2);

    // ARCore polygon is 2D (X, Z pairs), parse directly
    // Vertices are already in plane-local space relative to center
    for (int i = 0; i < polygonArraySize; i = i + 2) {
      VROVector3f vertex;
      vertex.x = polygonArray[i];
      vertex.y = 0; // ARCore polygons are 2D
      vertex.z = polygonArray[i + 1];
      boundaryVertices.push_back(vertex);
    }

    delete[] polygonArray;
  }
  plane->setBoundaryVertices(std::move(boundaryVertices));

  // Infer basic plane classification from PlaneType
  // Note: ARCore's semantic labels are not exposed in this API wrapper,
  // so we use heuristics based on plane orientation
  VROARPlaneClassification classification = VROARPlaneClassification::Unknown;
  switch (planeAR->getPlaneType()) {
  case arcore::PlaneType::HorizontalUpward:
    // Upward-facing horizontal planes are typically floors/ground
    classification = VROARPlaneClassification::Floor;
    break;
  case arcore::PlaneType::HorizontalDownward:
    // Downward-facing horizontal planes are typically ceilings
    classification = VROARPlaneClassification::Ceiling;
    break;
  case arcore::PlaneType::Vertical:
    // Vertical planes are typically walls (could also be doors/windows but we
    // can't distinguish)
    classification = VROARPlaneClassification::Wall;
    break;
  default:
    classification = VROARPlaneClassification::Unknown;
    break;
  }
  plane->setClassification(classification);

  // Record that an update occurred (for diagnostics)
  plane->recordUpdate(true);

#ifdef VRO_PLANE_PRECISION_DEBUG_LOGGING
  // PRECISION VALIDATION: Log comparison between ARCore raw data and ViroCore
  // processed data This helps validate that we're preserving native precision
  // WARNING: This logging happens on EVERY plane update and can severely impact
  // performance! Only enable for debugging precision issues.
  VROVector3f viroCenter = plane->getCenter();
  VROVector3f viroExtent = plane->getExtent();
  VROVector3f arcoreWorldCenter = transform.extractTranslation();
  VROMatrix4f viroTransform = plane->getTransform();
  VROVector3f viroTransformPosition = viroTransform.extractTranslation();

  pinfo("ARCore Plane Precision Check:");
  pinfo("  ARCore center pose (world): (%.6f, %.6f, %.6f)", arcoreWorldCenter.x,
        arcoreWorldCenter.y, arcoreWorldCenter.z);
  pinfo("  ViroCore plane.center (world): (%.6f, %.6f, %.6f) [should match "
        "ARCore]",
        viroCenter.x, viroCenter.y, viroCenter.z);
  pinfo("  ViroCore plane.transform position: (%.6f, %.6f, %.6f) [should match "
        "center]",
        viroTransformPosition.x, viroTransformPosition.y,
        viroTransformPosition.z);
  pinfo("  ARCore extent: %.6f x %.6f", extentX, extentZ);
  pinfo("  ViroCore extent: (%.6f, %.6f, %.6f)", viroExtent.x, viroExtent.y,
        viroExtent.z);
  pinfo("  Boundary vertices: %d", (int)boundaryVertices.size());
#endif
}

void VROARSessionARCore::syncImageAnchorWithARCore(
    std::shared_ptr<VROARImageAnchor> imageAnchor,
    arcore::AugmentedImage *imageAR) {
  arcore::Pose *pose = _session->createPose();
  imageAR->getCenterPose(pose);

  float newTransformMtx[16];
  pose->toMatrix(newTransformMtx);
  VROMatrix4f newTransform(newTransformMtx);
  imageAnchor->setTransform(newTransform);

  delete (pose);
}

uint8_t *VROARSessionARCore::getRotatedCameraImageData(int size) {
  if (_rotatedImageData == nullptr || _rotatedImageDataLength != size) {
    free(_rotatedImageData);
    _rotatedImageData = (uint8_t *)malloc(size);
    _rotatedImageDataLength = size;
  }
  return _rotatedImageData;
}

#pragma mark - Occlusion Support

void VROARSessionARCore::setOcclusionMode(VROOcclusionMode mode) {
  // Call base class to store the mode
  VROARSession::setOcclusionMode(mode);

  // Update ARCore depth mode based on occlusion mode
  arcore::DepthMode newDepthMode;
  switch (mode) {
    case VROOcclusionMode::DepthBased:
    case VROOcclusionMode::PeopleOnly:
      // Enable automatic depth for occlusion
      newDepthMode = arcore::DepthMode::Automatic;
      break;
    case VROOcclusionMode::Disabled:
    default:
      // Disable depth when occlusion is disabled
      newDepthMode = arcore::DepthMode::Disabled;
      break;
  }

  if (newDepthMode != _depthMode) {
    _depthMode = newDepthMode;

    // Only update config if session is ready
    if (_session != nullptr) {
      updateARCoreConfig();
      pinfo("VROARSessionARCore: Occlusion mode set to %d, depth mode set to %d",
            (int)mode, (int)_depthMode);
    } else {
      pinfo("VROARSessionARCore: Occlusion mode will be applied when session is ready (mode=%d, depth=%d)",
            (int)mode, (int)_depthMode);
    }
  }
}

bool VROARSessionARCore::isOcclusionSupported() const {
  if (_session == nullptr) {
    return false;
  }
  // Check if automatic depth mode is supported
  return _session->isDepthModeSupported(arcore::DepthMode::Automatic);
}

bool VROARSessionARCore::isOcclusionModeSupported(VROOcclusionMode mode) const {
  if (_session == nullptr) {
    return mode == VROOcclusionMode::Disabled;
  }

  switch (mode) {
    case VROOcclusionMode::Disabled:
      return true;
    case VROOcclusionMode::DepthBased:
      return _session->isDepthModeSupported(arcore::DepthMode::Automatic);
    case VROOcclusionMode::PeopleOnly:
      // People-only occlusion requires both depth and semantic segmentation
      return _session->isDepthModeSupported(arcore::DepthMode::Automatic) &&
             _session->isSemanticModeSupported(arcore::SemanticMode::Enabled);
    default:
      return false;
  }
}

#pragma mark - Geospatial API

bool VROARSessionARCore::isGeospatialModeSupported() const {
    if (!_session) return false;
    return _session->isGeospatialModeSupported(arcore::GeospatialMode::Enabled);
}

void VROARSessionARCore::setGeospatialModeEnabled(bool enabled) {
    if (!_session) return;

    arcore::GeospatialMode newMode = enabled ? arcore::GeospatialMode::Enabled : arcore::GeospatialMode::Disabled;

    // Avoid unnecessary reconfiguration if mode hasn't changed
    // This prevents VIO reset loops when setGeospatialModeEnabled is called repeatedly
    if (_geospatialMode == newMode) {
        return;
    }

    _geospatialMode = newMode;
    updateARCoreConfig();
}

VROEarthTrackingState VROARSessionARCore::getEarthTrackingState() const {
    if (!_session) return VROEarthTrackingState::Stopped;
    
    arcore::TrackingState state = _session->getEarthTrackingState();
    switch (state) {
        case arcore::TrackingState::Tracking:
            return VROEarthTrackingState::Tracking;
        case arcore::TrackingState::Paused:
            return VROEarthTrackingState::Paused;
        case arcore::TrackingState::Stopped:
        default:
            return VROEarthTrackingState::Stopped;
    }
}

VROGeospatialPose VROARSessionARCore::getCameraGeospatialPose() const {
    VROGeospatialPose result;
    if (!_session) return result;
    
    arcore::GeospatialPoseData poseData;
    if (_session->getCameraGeospatialPose(&poseData)) {
        result.latitude = poseData.latitude;
        result.longitude = poseData.longitude;
        result.altitude = poseData.altitude;
        result.heading = poseData.heading;
        result.horizontalAccuracy = poseData.horizontalAccuracy;
        result.verticalAccuracy = poseData.verticalAccuracy;
        result.orientationYawAccuracy = poseData.orientationYawAccuracy;
        result.quaternion = VROQuaternion(poseData.quaternion[0], poseData.quaternion[1], poseData.quaternion[2], poseData.quaternion[3]);
    }
    
    return result;
}

void VROARSessionARCore::checkVPSAvailability(double latitude, double longitude,
                                              std::function<void(VROVPSAvailability)> callback) {
    if (!_session) {
        if (callback) callback(VROVPSAvailability::Unknown);
        return;
    }

    _session->checkVpsAvailability(latitude, longitude, [callback](arcore::VPSAvailability availability) {
        VROVPSAvailability result;
        switch (availability) {
            case arcore::VPSAvailability::Available:
                result = VROVPSAvailability::Available;
                break;
            case arcore::VPSAvailability::Unavailable:
                result = VROVPSAvailability::Unavailable;
                break;
            case arcore::VPSAvailability::ErrorNetwork:
                result = VROVPSAvailability::ErrorNetwork;
                break;
            case arcore::VPSAvailability::ErrorResourceExhausted:
                result = VROVPSAvailability::ErrorResourceExhausted;
                break;
            case arcore::VPSAvailability::ErrorInternal:
            case arcore::VPSAvailability::Unknown:
            default:
                result = VROVPSAvailability::Unknown;
                break;
        }
        if (callback) callback(result);
    });
}

void VROARSessionARCore::createGeospatialAnchor(double latitude, double longitude, double altitude,
                                                VROQuaternion quaternion,
                                                std::function<void(std::shared_ptr<VROGeospatialAnchor>)> onSuccess,
                                                std::function<void(std::string error)> onFailure) {
    if (_geospatialMode == arcore::GeospatialMode::Disabled) {
        if (onFailure) onFailure("Geospatial mode is disabled");
        return;
    }

    // Native takes double lat, double lon, double alt, float* rotation (4 floats)
    float rot[4] = {quaternion.X, quaternion.Y, quaternion.Z, quaternion.W};
    
    arcore::Anchor *nativeAnchor = _session->createGeospatialAnchor(latitude, longitude, altitude, rot[0], rot[1], rot[2], rot[3]);
    
    if (nativeAnchor) {
        std::shared_ptr<arcore::Anchor> anchorShared(nativeAnchor);
        std::string key = VROStringUtil::toString64(anchorShared->getId());

        // Create VROGeospatialAnchor with the same ID as the native anchor
        std::shared_ptr<VROGeospatialAnchor> geoAnchor = std::make_shared<VROGeospatialAnchor>(
            VROGeospatialAnchorType::WGS84, latitude, longitude, altitude, quaternion);
        geoAnchor->setId(key);

        // Create VROARAnchorARCore
        // We use the geoAnchor as the "trackable" so we can retrieve it in processUpdatedAnchors
        std::shared_ptr<VROARAnchorARCore> vAnchor = std::make_shared<VROARAnchorARCore>(
            key, anchorShared, geoAnchor, shared_from_this());

        // Add to maps
        addAnchor(vAnchor);

        // Invoke callback
        if (onSuccess) onSuccess(geoAnchor);
    } else {
        if (onFailure) onFailure("Failed to create geospatial anchor");
    }
}

void VROARSessionARCore::createTerrainAnchor(double latitude, double longitude, double altitudeAboveTerrain,
                                             VROQuaternion quaternion,
                                             std::function<void(std::shared_ptr<VROGeospatialAnchor>)> onSuccess,
                                             std::function<void(std::string error)> onFailure) {
    if (_geospatialMode == arcore::GeospatialMode::Disabled) {
        if (onFailure) onFailure("Geospatial mode is disabled");
        return;
    }

    std::weak_ptr<VROARSessionARCore> weakSession = shared_from_this();
    
    _session->createTerrainAnchor(latitude, longitude, altitudeAboveTerrain, quaternion.X, quaternion.Y, quaternion.Z, quaternion.W,
        [weakSession, latitude, longitude, altitudeAboveTerrain, quaternion, onSuccess](arcore::Anchor *nativeAnchor) {
            VROPlatformDispatchAsyncRenderer([weakSession, latitude, longitude, altitudeAboveTerrain, quaternion, onSuccess, nativeAnchor] {
                std::shared_ptr<VROARSessionARCore> session = weakSession.lock();
                if (!session) {
                    delete nativeAnchor;
                    return;
                }

                // Create VROARAnchorARCore wrapper
                std::shared_ptr<arcore::Anchor> anchorShared(nativeAnchor);
                std::string key = VROStringUtil::toString64(anchorShared->getId());

                // Create VROGeospatialAnchor with the same ID as the native anchor
                std::shared_ptr<VROGeospatialAnchor> geoAnchor = std::make_shared<VROGeospatialAnchor>(
                    VROGeospatialAnchorType::Terrain, latitude, longitude, altitudeAboveTerrain, quaternion);
                geoAnchor->setId(key);
                geoAnchor->setResolveState(VROGeospatialAnchorResolveState::Success);

                // Create VROARAnchorARCore
                std::shared_ptr<VROARAnchorARCore> vAnchor = std::make_shared<VROARAnchorARCore>(
                    key, anchorShared, geoAnchor, session);

                // Add to maps
                session->addAnchor(vAnchor);

                if (onSuccess) onSuccess(geoAnchor);
            });
        },
        [onFailure](std::string error) {
            VROPlatformDispatchAsyncRenderer([onFailure, error] {
                if (onFailure) onFailure(error);
            });
        }
    );
}

void VROARSessionARCore::createRooftopAnchor(double latitude, double longitude, double altitudeAboveRooftop,
                                             VROQuaternion quaternion,
                                             std::function<void(std::shared_ptr<VROGeospatialAnchor>)> onSuccess,
                                             std::function<void(std::string error)> onFailure) {
    if (_geospatialMode == arcore::GeospatialMode::Disabled) {
        if (onFailure) onFailure("Geospatial mode is disabled");
        return;
    }

    std::weak_ptr<VROARSessionARCore> weakSession = shared_from_this();
    
    _session->createRooftopAnchor(latitude, longitude, altitudeAboveRooftop, quaternion.X, quaternion.Y, quaternion.Z, quaternion.W,
        [weakSession, latitude, longitude, altitudeAboveRooftop, quaternion, onSuccess](arcore::Anchor *nativeAnchor) {
            VROPlatformDispatchAsyncRenderer([weakSession, latitude, longitude, altitudeAboveRooftop, quaternion, onSuccess, nativeAnchor] {
                std::shared_ptr<VROARSessionARCore> session = weakSession.lock();
                if (!session) {
                    delete nativeAnchor;
                    return;
                }

                // Create VROARAnchorARCore wrapper
                std::shared_ptr<arcore::Anchor> anchorShared(nativeAnchor);
                std::string key = VROStringUtil::toString64(anchorShared->getId());

                // Create VROGeospatialAnchor with the same ID as the native anchor
                std::shared_ptr<VROGeospatialAnchor> geoAnchor = std::make_shared<VROGeospatialAnchor>(
                    VROGeospatialAnchorType::Rooftop, latitude, longitude, altitudeAboveRooftop, quaternion);
                geoAnchor->setId(key);
                geoAnchor->setResolveState(VROGeospatialAnchorResolveState::Success);

                // Create VROARAnchorARCore
                std::shared_ptr<VROARAnchorARCore> vAnchor = std::make_shared<VROARAnchorARCore>(
                    key, anchorShared, geoAnchor, session);

                // Add to maps
                session->addAnchor(vAnchor);

                if (onSuccess) onSuccess(geoAnchor);
            });
        },
        [onFailure](std::string error) {
            VROPlatformDispatchAsyncRenderer([onFailure, error] {
                if (onFailure) onFailure(error);
            });
        }
    );
}

void VROARSessionARCore::removeGeospatialAnchor(std::shared_ptr<VROGeospatialAnchor> anchor) {
    if (!anchor) return;

    std::shared_ptr<VROARAnchorARCore> foundAnchor = nullptr;
    for (std::shared_ptr<VROARAnchorARCore> vAnchor : _anchors) {
        if (vAnchor->getTrackable() == anchor) {
            foundAnchor = vAnchor;
            break;
        }
    }

    if (foundAnchor) {
        // We must detach the native anchor first
        if (foundAnchor->getAnchorInternal()) {
            foundAnchor->getAnchorInternal()->detach();
        }
        removeAnchor(foundAnchor);
    }
}

#pragma mark - Scene Semantics API

bool VROARSessionARCore::isSemanticModeSupported() const {
    if (!_session) return false;

    // Check if ARCore supports semantic mode on this device
    // This also validates that the ARCore version is 1.40+
    return _session->isSemanticModeSupported(arcore::SemanticMode::Enabled);
}

void VROARSessionARCore::setSemanticModeEnabled(bool enabled) {
    pinfo("setSemanticModeEnabled called with enabled=%s (current: _semanticMode=%d, _semanticModeEnabled=%s)",
          enabled ? "true" : "false", (int)_semanticMode, _semanticModeEnabled ? "true" : "false");

    if (!_session) {
        pwarn("setSemanticModeEnabled: No session, returning early");
        return;
    }

    arcore::SemanticMode newMode = enabled ? arcore::SemanticMode::Enabled : arcore::SemanticMode::Disabled;

    // Avoid unnecessary reconfiguration if mode hasn't changed
    // Check both the ARCore mode AND the enabled flag to handle initialization properly
    if (_semanticMode == newMode && _semanticModeEnabled == enabled) {
        pinfo("setSemanticModeEnabled: Mode unchanged, returning early");
        return;
    }

    // Check if semantic mode is supported before enabling
    if (enabled && !isSemanticModeSupported()) {
        pwarn("⚠️ Scene Semantics is not supported on this device, ignoring setSemanticModeEnabled(true)");
        return;
    }

    _semanticMode = newMode;
    _semanticModeEnabled = enabled;
    pinfo("setSemanticModeEnabled: About to call updateARCoreConfig with _semanticMode=%d", (int)_semanticMode);

    if (updateARCoreConfig()) {
        pinfo("Scene Semantics mode set to %s", enabled ? "ENABLED" : "DISABLED");
    } else {
        pwarn("⚠️ Failed to update ARCore config for Scene Semantics");
        // Revert the state
        _semanticMode = arcore::SemanticMode::Disabled;
        _semanticModeEnabled = false;
    }
}

void VROARSessionARCore::updateDepthTexture() {
    if (!isDepthModeEnabled()) {
        return;
    }

    // Acquire depth image from ARCore
    arcore::Image *depthImage = nullptr;
    arcore::ImageRetrievalStatus status = _frame->acquireDepthImage(&depthImage);

    if (status != arcore::ImageRetrievalStatus::Success || depthImage == nullptr) {
        // pwarn("VROARSessionARCore: Failed to acquire depth image. Status: %d", (int)status);
        return;
    }

    int width = depthImage->getWidth();
    int height = depthImage->getHeight();

    if (width <= 0 || height <= 0) {
        pwarn("VROARSessionARCore: Invalid depth image dimensions: %d x %d", width, height);
        delete depthImage;
        return;
    }

    // Get depth data (16-bit depth in millimeters)
    const uint8_t *depthData = nullptr;
    int depthDataLength = 0;
    depthImage->getPlaneData(0, &depthData, &depthDataLength);

    int rowStride = depthImage->getPlaneRowStride(0);

    if (depthData == nullptr || depthDataLength <= 0) {
        pwarn("VROARSessionARCore: Invalid depth data. Length: %d", depthDataLength);
        delete depthImage;
        return;
    }

    // Convert to float buffer
    int numPixels = width * height;
    if (_depthFloatBuffer.size() != numPixels) {
        _depthFloatBuffer.resize(numPixels);
    }

    float *floatData = _depthFloatBuffer.data();
    
    // Handle row stride (padding)
    if (rowStride > 0 && rowStride != width * 2) {
        for (int y = 0; y < height; y++) {
            const uint16_t *rowStart = reinterpret_cast<const uint16_t*>(depthData + y * rowStride);
            for (int x = 0; x < width; x++) {
                floatData[y * width + x] = (float)rowStart[x] * 0.001f;
            }
        }
    } else {
        // Optimized loop: direct pointer access (packed)
        const uint16_t *depthData16 = reinterpret_cast<const uint16_t*>(depthData);
        for (int i = 0; i < numPixels; i++) {
            floatData[i] = (float)depthData16[i] * 0.001f; // mm to meters
        }
    }

    // If texture doesn't exist, create it
    if (!_depthTexture || _depthTexture->getWidth() != width || _depthTexture->getHeight() != height) {
        pinfo("VROARSessionARCore: Creating new depth texture (size %d x %d)", width, height);
        std::shared_ptr<VROData> depthVROData = std::make_shared<VROData>(
            (void *)floatData, numPixels * sizeof(float), VRODataOwnership::Copy);
        std::vector<std::shared_ptr<VROData>> dataVec = { depthVROData };

        _depthTexture = std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                                      VROTextureFormat::R32F,
                                                      VROTextureInternalFormat::R32F,
                                                      false,
                                                      VROMipmapMode::None,
                                                      dataVec,
                                                      width, height,
                                                      std::vector<uint32_t>());
        
        _depthTexture->setMinificationFilter(VROFilterMode::Nearest);
        _depthTexture->setMagnificationFilter(VROFilterMode::Nearest);
        // Use Clamp which maps to GL_CLAMP_TO_EDGE for OpenGL ES compatibility
        _depthTexture->setWrapS(VROWrapMode::Clamp);
        _depthTexture->setWrapT(VROWrapMode::Clamp);
    } else {
        // Update existing texture
        std::shared_ptr<VRODriver> driver = _driver.lock();
        if (driver) {
            VROTextureSubstrate *substrate = _depthTexture->getSubstrate(0, driver, true);
            if (substrate) {
                // We need to cast to OpenGL substrate to access GL ID, or use a generic update method if available.
                // Since we are in VROARSessionARCore (Android specific), we can assume OpenGL.
                VROTextureSubstrateOpenGL *glSubstrate = (VROTextureSubstrateOpenGL *)substrate;
                std::pair<GLenum, GLuint> textureInfo = glSubstrate->getTexture();
                GLenum target = textureInfo.first;
                GLuint texId = textureInfo.second;
                
                glBindTexture(target, texId);
                glTexSubImage2D(target, 0, 0, 0, width, height, GL_RED, GL_FLOAT, floatData);
                glBindTexture(target, 0);
            } else {
                pwarn("VROARSessionARCore: Failed to get substrate for depth texture update");
            }
        } else {
            pwarn("VROARSessionARCore: Driver expired, cannot update depth texture");
        }
    }

    delete depthImage;
}
