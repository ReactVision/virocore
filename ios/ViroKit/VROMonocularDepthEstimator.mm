//
//  VROMonocularDepthEstimator.mm
//  ViroKit
//
//  Copyright © 2024 Viro Media. All rights reserved.
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
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 140000

#include "VROMonocularDepthEstimator.h"
#include "VROARFrameiOS.h"
#include "VRODriver.h"
#include "VROData.h"
#include "VROLog.h"
#include "VROTime.h"
#include "VROConvert.h"
#include <cmath>

#pragma mark - Lifecycle

VROMonocularDepthEstimator::VROMonocularDepthEstimator(std::shared_ptr<VRODriver> driver) :
    _driver(driver),
    _model(nil),
    _coreMLModel(nil),
    _visionRequest(nil),
    _modelLoaded(false),
    _processingImage(nullptr),
    _nextImage(nullptr),
    _isProcessing(false),
    _depthWidth(0),
    _depthHeight(0),
    _depthScaleFactor(1.0f),
    _temporalFilteringEnabled(true),
    _temporalFilterAlpha(0.3f),
    _targetFPS(15),
    _lastInferenceTime(0),
    _currentFPS(0),
    _averageLatencyMs(0),
    _frameCount(0),
    _fpsAccumulator(0),
    _latencyAccumulator(0) {

    // Create serial dispatch queue for depth inference
    _depthQueue = dispatch_queue_create("com.viro.depthQueue", DISPATCH_QUEUE_SERIAL);

    // Set high QoS for responsive depth updates
    dispatch_set_target_queue(_depthQueue,
        dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));

    _depthTextureTransform = VROMatrix4f::identity();
}

VROMonocularDepthEstimator::~VROMonocularDepthEstimator() {
    // Release any retained pixel buffers
    if (_processingImage) {
        CVBufferRelease(_processingImage);
        _processingImage = nullptr;
    }
    if (_nextImage) {
        CVBufferRelease(_nextImage);
        _nextImage = nullptr;
    }

    // Release CoreML objects
    _visionRequest = nil;
    _coreMLModel = nil;
    _model = nil;
}

#pragma mark - Initialization

bool VROMonocularDepthEstimator::initWithModel(NSString *modelPath) {
    if (_modelLoaded) {
        pinfo("VROMonocularDepthEstimator: Model already loaded");
        return true;
    }

    if (!modelPath || ![[NSFileManager defaultManager] fileExistsAtPath:modelPath]) {
        pwarn("VROMonocularDepthEstimator: Model path does not exist: %s",
              modelPath ? [modelPath UTF8String] : "nil");
        return false;
    }

    pinfo("VROMonocularDepthEstimator: Loading model from %s", [modelPath UTF8String]);

    NSError *error = nil;

    // Create model configuration optimized for depth estimation
    MLModelConfiguration *config = [[MLModelConfiguration alloc] init];
    config.computeUnits = MLComputeUnitsAll; // Use Neural Engine when available

    // Load the compiled CoreML model
    NSURL *modelURL = [NSURL fileURLWithPath:modelPath];
    _model = [MLModel modelWithContentsOfURL:modelURL configuration:config error:&error];

    if (error || !_model) {
        pwarn("VROMonocularDepthEstimator: Failed to load model: %s",
              error ? [[error localizedDescription] UTF8String] : "unknown error");

        std::shared_ptr<VROMonocularDepthEstimatorDelegate> delegate = _delegate.lock();
        if (delegate) {
            delegate->onDepthModelLoadError(error ? [[error localizedDescription] UTF8String] : "unknown error");
        }
        return false;
    }

    // Wrap in Vision framework model
    _coreMLModel = [VNCoreMLModel modelForMLModel:_model error:&error];

    if (error || !_coreMLModel) {
        pwarn("VROMonocularDepthEstimator: Failed to create VNCoreMLModel: %s",
              error ? [[error localizedDescription] UTF8String] : "unknown error");
        _model = nil;

        std::shared_ptr<VROMonocularDepthEstimatorDelegate> delegate = _delegate.lock();
        if (delegate) {
            delegate->onDepthModelLoadError(error ? [[error localizedDescription] UTF8String] : "unknown error");
        }
        return false;
    }

    // Create Vision request with completion handler
    // Capture 'this' pointer for use in the block
    // Note: Caller must ensure this object outlives async operations
    VROMonocularDepthEstimator *estimator = this;

    _visionRequest = [[VNCoreMLRequest alloc] initWithModel:_coreMLModel
        completionHandler:^(VNRequest *request, NSError *requestError) {
            if (requestError) {
                pwarn("VROMonocularDepthEstimator: Inference error: %s",
                      [[requestError localizedDescription] UTF8String]);
                return;
            }

            // Extract the depth output
            NSArray *results = request.results;
            if (results.count == 0) {
                pwarn("VROMonocularDepthEstimator: No results from inference");
                return;
            }

            VNCoreMLFeatureValueObservation *observation =
                (VNCoreMLFeatureValueObservation *)results.firstObject;

            // Get the transform that was stored before inference
            // Note: We need to pass this through - using instance variable for now
            if (estimator) {
                estimator->processDepthOutput(observation, estimator->_nextTransform);
            }
        }];

    // Configure the request for optimal depth estimation
    _visionRequest.imageCropAndScaleOption = VNImageCropAndScaleOptionScaleFill;

    _modelLoaded = true;
    pinfo("VROMonocularDepthEstimator: Model loaded successfully");

    // Notify delegate
    std::shared_ptr<VROMonocularDepthEstimatorDelegate> delegate = _delegate.lock();
    if (delegate) {
        delegate->onDepthModelReady();
    }

    return true;
}

bool VROMonocularDepthEstimator::isAvailable() const {
    return _modelLoaded && _model != nil && _coreMLModel != nil;
}

bool VROMonocularDepthEstimator::isSupported() {
    if (@available(iOS 14.0, *)) {
        return true;
    }
    return false;
}

#pragma mark - Frame Processing

void VROMonocularDepthEstimator::update(const VROARFrame *frame) {
    if (!isAvailable()) {
        return;
    }

    // Rate limiting - skip if we're processing too fast
    double currentTime = VROTimeCurrentMillis();
    if (_targetFPS > 0) {
        double targetInterval = 1000.0 / _targetFPS;
        if ((currentTime - _lastInferenceTime) < targetInterval) {
            return;
        }
    }

    // Cast to iOS frame to get the camera image
    const VROARFrameiOS *frameiOS = dynamic_cast<const VROARFrameiOS *>(frame);
    if (!frameiOS) {
        return;
    }

    CVPixelBufferRef image = frameiOS->getImage();
    if (!image) {
        return;
    }

    // Store the image and transform for processing
    {
        std::lock_guard<std::mutex> lock(_imageMutex);

        // Release previous next image if any
        if (_nextImage) {
            CVBufferRelease(_nextImage);
        }

        _nextImage = CVBufferRetain(image);
        _nextTransform = frame->getViewportToCameraImageTransform().invert();
        _nextOrientation = frameiOS->getImageOrientation();
    }

    // Dispatch inference to depth queue
    std::weak_ptr<VROMonocularDepthEstimator> weak_self = shared_from_this();

    dispatch_async(_depthQueue, ^{
        std::shared_ptr<VROMonocularDepthEstimator> strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->nextImage();
        }
    });
}

void VROMonocularDepthEstimator::nextImage() {
    // Check if we're already processing
    bool expected = false;
    if (!_isProcessing.compare_exchange_strong(expected, true)) {
        return; // Already processing, skip this frame
    }

    CVPixelBufferRef imageToProcess = nullptr;
    VROMatrix4f transform;
    CGImagePropertyOrientation orientation;

    // Get the next image under lock
    {
        std::lock_guard<std::mutex> lock(_imageMutex);

        if (!_nextImage) {
            _isProcessing = false;
            return;
        }

        imageToProcess = _nextImage;
        _nextImage = nullptr;
        transform = _nextTransform;
        orientation = _nextOrientation;
    }

    // Store as processing image (we own this reference now)
    if (_processingImage) {
        CVBufferRelease(_processingImage);
    }
    _processingImage = imageToProcess;

    // Run inference
    runInference(imageToProcess, transform, orientation);
}

void VROMonocularDepthEstimator::runInference(CVPixelBufferRef image,
                                               VROMatrix4f transform,
                                               CGImagePropertyOrientation orientation) {
    double startTime = VROTimeCurrentMillis();

    // Create CIImage from pixel buffer
    CIImage *ciImage = [[CIImage alloc] initWithCVPixelBuffer:image];

    // Create request handler
    NSDictionary *options = @{};
    VNImageRequestHandler *handler = [[VNImageRequestHandler alloc]
        initWithCIImage:ciImage
        orientation:orientation
        options:options];

    // Perform inference
    NSError *error = nil;
    [handler performRequests:@[_visionRequest] error:&error];

    if (error) {
        pwarn("VROMonocularDepthEstimator: Request failed: %s",
              [[error localizedDescription] UTF8String]);
    }

    // Update diagnostics
    double inferenceTime = VROTimeCurrentMillis() - startTime;
    updateDiagnostics(inferenceTime);

    // Mark as done processing
    _isProcessing = false;

    // Check if there's another frame waiting
    std::weak_ptr<VROMonocularDepthEstimator> weak_self = shared_from_this();
    dispatch_async(_depthQueue, ^{
        std::shared_ptr<VROMonocularDepthEstimator> strong_self = weak_self.lock();
        if (strong_self) {
            std::lock_guard<std::mutex> lock(strong_self->_imageMutex);
            if (strong_self->_nextImage) {
                strong_self->nextImage();
            }
        }
    });
}

#pragma mark - Depth Output Processing

void VROMonocularDepthEstimator::processDepthOutput(VNCoreMLFeatureValueObservation *result,
                                                     VROMatrix4f transform) {
    if (!result || !result.featureValue) {
        return;
    }

    MLMultiArray *depthArray = result.featureValue.multiArrayValue;
    if (!depthArray) {
        pwarn("VROMonocularDepthEstimator: No depth array in result");
        return;
    }

    // Get dimensions - expect shape [1, H, W] or [H, W]
    NSArray<NSNumber *> *shape = depthArray.shape;
    int height, width;

    if (shape.count == 3) {
        height = shape[1].intValue;
        width = shape[2].intValue;
    } else if (shape.count == 2) {
        height = shape[0].intValue;
        width = shape[1].intValue;
    } else {
        pwarn("VROMonocularDepthEstimator: Unexpected shape with %lu dimensions",
              (unsigned long)shape.count);
        return;
    }

    // Resize buffer if needed
    size_t bufferSize = width * height;
    if (_depthBuffer.size() != bufferSize) {
        _depthBuffer.resize(bufferSize);
        _previousDepthBuffer.clear(); // Reset temporal filter
    }

    // Copy and scale depth data
    float *rawDepth = (float *)depthArray.dataPointer;
    for (size_t i = 0; i < bufferSize; i++) {
        _depthBuffer[i] = rawDepth[i] * _depthScaleFactor;
    }

    // Apply temporal filtering if enabled
    if (_temporalFilteringEnabled) {
        applyTemporalFilter(_depthBuffer.data(), width, height);
    }

    // Store dimensions
    _depthWidth = width;
    _depthHeight = height;

    // Compute texture transform
    int imageWidth = _processingImage ? (int)CVPixelBufferGetWidth(_processingImage) : 1920;
    int imageHeight = _processingImage ? (int)CVPixelBufferGetHeight(_processingImage) : 1440;
    _depthTextureTransform = computeDepthTextureTransform(transform, imageWidth, imageHeight,
                                                          width, height);

    // Update GPU texture
    updateDepthTexture(_depthBuffer.data(), width, height);

    _lastInferenceTime = VROTimeCurrentMillis();
}

void VROMonocularDepthEstimator::applyTemporalFilter(float *depthData, int width, int height) {
    size_t bufferSize = width * height;

    // Initialize previous buffer on first frame
    if (_previousDepthBuffer.empty()) {
        _previousDepthBuffer.assign(depthData, depthData + bufferSize);
        return;
    }

    // Ensure previous buffer matches current size
    if (_previousDepthBuffer.size() != bufferSize) {
        _previousDepthBuffer.assign(depthData, depthData + bufferSize);
        return;
    }

    const float alpha = _temporalFilterAlpha;
    const float edgeThreshold = 0.3f; // 30cm discontinuity threshold

    for (size_t i = 0; i < bufferSize; i++) {
        float prevDepth = _previousDepthBuffer[i];
        float currDepth = depthData[i];

        // Skip filtering at depth discontinuities
        float diff = std::abs(currDepth - prevDepth);
        if (diff < edgeThreshold && prevDepth > 0 && currDepth > 0) {
            // Exponential moving average
            depthData[i] = prevDepth + alpha * (currDepth - prevDepth);
        }
        // else: keep current depth (edge or invalid previous)

        _previousDepthBuffer[i] = depthData[i];
    }
}

void VROMonocularDepthEstimator::updateDepthTexture(const float *depthData, int width, int height) {
    std::lock_guard<std::mutex> lock(_depthMutex);

    std::shared_ptr<VRODriver> driver = _driver.lock();
    if (!driver) {
        return;
    }

    // Create or update texture
    bool needsNewTexture = !_currentDepthTexture ||
                           _currentDepthTexture->getWidth() != width ||
                           _currentDepthTexture->getHeight() != height;

    if (needsNewTexture) {
        // Create new texture
        size_t dataSize = width * height * sizeof(float);
        std::shared_ptr<VROData> depthVROData = std::make_shared<VROData>(
            (void *)depthData, dataSize, VRODataOwnership::Copy);
        std::vector<std::shared_ptr<VROData>> dataVec = { depthVROData };

        _currentDepthTexture = std::make_shared<VROTexture>(
            VROTextureType::Texture2D,
            VROTextureFormat::R32F,
            VROTextureInternalFormat::R32F,
            false,  // not sRGB
            VROMipmapMode::None,
            dataVec,
            width, height,
            std::vector<uint32_t>());

        _currentDepthTexture->setMinificationFilter(VROFilterMode::Linear);
        _currentDepthTexture->setMagnificationFilter(VROFilterMode::Linear);
        _currentDepthTexture->setWrapS(VROWrapMode::Clamp);
        _currentDepthTexture->setWrapT(VROWrapMode::Clamp);

        pinfo("VROMonocularDepthEstimator: Created depth texture %dx%d", width, height);
    } else {
        // Update existing texture data
        // Note: VROTexture doesn't have updateData, so we recreate
        // TODO: Add updateData method to VROTexture for efficiency
        size_t dataSize = width * height * sizeof(float);
        std::shared_ptr<VROData> depthVROData = std::make_shared<VROData>(
            (void *)depthData, dataSize, VRODataOwnership::Copy);
        std::vector<std::shared_ptr<VROData>> dataVec = { depthVROData };

        _currentDepthTexture = std::make_shared<VROTexture>(
            VROTextureType::Texture2D,
            VROTextureFormat::R32F,
            VROTextureInternalFormat::R32F,
            false,
            VROMipmapMode::None,
            dataVec,
            width, height,
            std::vector<uint32_t>());

        _currentDepthTexture->setMinificationFilter(VROFilterMode::Linear);
        _currentDepthTexture->setMagnificationFilter(VROFilterMode::Linear);
        _currentDepthTexture->setWrapS(VROWrapMode::Clamp);
        _currentDepthTexture->setWrapT(VROWrapMode::Clamp);
    }
}

VROMatrix4f VROMonocularDepthEstimator::computeDepthTextureTransform(VROMatrix4f imageTransform,
                                                                      int imageWidth, int imageHeight,
                                                                      int depthWidth, int depthHeight) {
    // The depth texture may have a different resolution than the camera image.
    // We need to compose the image-to-screen transform with a scaling transform.

    // For now, assume same aspect ratio and just use the image transform
    // TODO: Handle different aspect ratios properly
    return imageTransform;
}

#pragma mark - Depth Output Accessors

std::shared_ptr<VROTexture> VROMonocularDepthEstimator::getDepthTexture() {
    std::lock_guard<std::mutex> lock(_depthMutex);
    return _currentDepthTexture;
}

VROMatrix4f VROMonocularDepthEstimator::getDepthTextureTransform() const {
    return _depthTextureTransform;
}

void VROMonocularDepthEstimator::getDepthDimensions(int *outWidth, int *outHeight) const {
    if (outWidth) *outWidth = _depthWidth;
    if (outHeight) *outHeight = _depthHeight;
}

#pragma mark - Configuration

void VROMonocularDepthEstimator::setScaleFactor(float scale) {
    _depthScaleFactor = scale;
}

void VROMonocularDepthEstimator::setTemporalFilteringEnabled(bool enabled) {
    _temporalFilteringEnabled = enabled;
    if (!enabled) {
        _previousDepthBuffer.clear();
    }
}

void VROMonocularDepthEstimator::setTemporalFilterAlpha(float alpha) {
    _temporalFilterAlpha = std::max(0.0f, std::min(1.0f, alpha));
}

void VROMonocularDepthEstimator::setTargetFPS(int fps) {
    _targetFPS = std::max(0, fps);
}

void VROMonocularDepthEstimator::setDelegate(std::weak_ptr<VROMonocularDepthEstimatorDelegate> delegate) {
    _delegate = delegate;
}

#pragma mark - Diagnostics

float VROMonocularDepthEstimator::getCurrentFPS() const {
    return _currentFPS;
}

float VROMonocularDepthEstimator::getAverageLatencyMs() const {
    return _averageLatencyMs;
}

void VROMonocularDepthEstimator::updateDiagnostics(double inferenceTimeMs) {
    _frameCount++;
    _latencyAccumulator += inferenceTimeMs;

    // Update FPS every 10 frames
    if (_frameCount >= 10) {
        _averageLatencyMs = _latencyAccumulator / _frameCount;
        _currentFPS = 1000.0f / _averageLatencyMs;

        _frameCount = 0;
        _latencyAccumulator = 0;
    }
}

#endif // __IPHONE_OS_VERSION_MAX_ALLOWED >= 140000
