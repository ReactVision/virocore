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
#include "VROTextureSubstrate.h"
#include "VRODriver.h"
#include "VROData.h"
#include "VROLog.h"
#include "VROTime.h"
#include "VROConvert.h"
#import <Metal/Metal.h>
#include <cmath>

// Off-by-default flag gating hot-path (per-frame / per-inference) diagnostic logs.
static const bool kDebugMonoDepth = false;

// IEEE 754 half-precision float (16-bit) to 32-bit float conversion
static inline float float16ToFloat32(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x03FF;

    if (exponent == 0) {
        if (mantissa == 0) {
            // Zero
            uint32_t result = sign;
            float f;
            memcpy(&f, &result, sizeof(f));
            return f;
        }
        // Subnormal: normalize
        while (!(mantissa & 0x0400)) {
            mantissa <<= 1;
            exponent--;
        }
        exponent++;
        mantissa &= ~0x0400;
    } else if (exponent == 31) {
        // Inf/NaN
        uint32_t result = sign | 0x7F800000 | (mantissa << 13);
        float f;
        memcpy(&f, &result, sizeof(f));
        return f;
    }

    uint32_t result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float f;
    memcpy(&f, &result, sizeof(f));
    return f;
}

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
    _calibrationMode(VROMonocularDepthCalibration::Manual),
    _hitTestConfidenceThreshold(0.3f),
    _temporalFilteringEnabled(true),
    _temporalFilterAlpha(0.3f),
    _targetFPS(5),   // 5fps: sufficient for AR occlusion, halves ANE thermal load vs default 15
    _lastInferenceTime(0),
    _currentFPS(0),
    _averageLatencyMs(0),
    _frameCount(0),
    _fpsAccumulator(0),
    _latencyAccumulator(0),
    _stagingDirty(false) {

    // Create serial dispatch queue for depth inference
    _depthQueue = dispatch_queue_create("com.viro.depthQueue", DISPATCH_QUEUE_SERIAL);

    // UTILITY QoS: background ML inference shouldn't compete with rendering.
    // The OS schedules utility work on efficiency cores when available (A14+),
    // which dramatically reduces heat compared to USER_INITIATED.
    dispatch_set_target_queue(_depthQueue,
        dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));

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
                // Ensure we release the processing image on error to avoid retaining ARFrames
                if (estimator && estimator->_processingImage) {
                    CVBufferRelease(estimator->_processingImage);
                    estimator->_processingImage = nullptr;
                }
                return;
            }

            // Extract the depth output
            NSArray *results = request.results;
            if (results.count == 0) {
                pwarn("VROMonocularDepthEstimator: No results from inference");
                // Ensure we release the processing image on error to avoid retaining ARFrames
                if (estimator && estimator->_processingImage) {
                    CVBufferRelease(estimator->_processingImage);
                    estimator->_processingImage = nullptr;
                }
                return;
            }

            // Log all results to find the correct output
            if (kDebugMonoDepth) {
                static bool loggedResults = false;
                if (!loggedResults) {
                    loggedResults = true;
                    NSLog(@"[VIRO_MONO_DEBUG] === Inference Results: %lu observations ===", (unsigned long)results.count);
                    for (NSUInteger i = 0; i < results.count; i++) {
                        VNObservation *obs = results[i];
                        NSLog(@"[VIRO_MONO_DEBUG] Result[%lu]: class=%@, confidence=%.4f",
                              (unsigned long)i, NSStringFromClass([obs class]), obs.confidence);
                        if ([obs isKindOfClass:[VNCoreMLFeatureValueObservation class]]) {
                            VNCoreMLFeatureValueObservation *fvo = (VNCoreMLFeatureValueObservation *)obs;
                            NSLog(@"[VIRO_MONO_DEBUG]   featureName='%@'", fvo.featureName);
                            if (fvo.featureValue.multiArrayValue) {
                                MLMultiArray *arr = fvo.featureValue.multiArrayValue;
                                NSLog(@"[VIRO_MONO_DEBUG]   shape=%@, dataType=%ld, strides=%@",
                                      arr.shape, (long)arr.dataType, arr.strides);
                                // Sample a few raw bytes to verify data exists
                                NSLog(@"[VIRO_MONO_DEBUG]   dataPointer=%p, total elements=%ld",
                                      arr.dataPointer, (long)(arr.count));
                                if (arr.dataPointer && arr.count > 0) {
                                    uint8_t *bytes = (uint8_t *)arr.dataPointer;
                                    NSLog(@"[VIRO_MONO_DEBUG]   First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                                          bytes[0], bytes[1], bytes[2], bytes[3],
                                          bytes[4], bytes[5], bytes[6], bytes[7],
                                          bytes[8], bytes[9], bytes[10], bytes[11],
                                          bytes[12], bytes[13], bytes[14], bytes[15]);
                                    // Also check bytes at offset 1000 (middle of buffer)
                                    size_t midOffset = arr.count; // element count, check byte at ~mid
                                    if (midOffset > 100) midOffset = 1000;
                                    NSLog(@"[VIRO_MONO_DEBUG]   Bytes at offset %zu: %02X %02X %02X %02X %02X %02X %02X %02X",
                                          midOffset,
                                          bytes[midOffset], bytes[midOffset+1], bytes[midOffset+2], bytes[midOffset+3],
                                          bytes[midOffset+4], bytes[midOffset+5], bytes[midOffset+6], bytes[midOffset+7]);
                                }
                            }
                            if (fvo.featureValue.imageBufferValue) {
                                CVPixelBufferRef pb = fvo.featureValue.imageBufferValue;
                                NSLog(@"[VIRO_MONO_DEBUG]   pixelBuffer: %zux%zu, format=%u",
                                      CVPixelBufferGetWidth(pb), CVPixelBufferGetHeight(pb),
                                      (unsigned)CVPixelBufferGetPixelFormatType(pb));
                            }
                        } else if ([obs isKindOfClass:[VNPixelBufferObservation class]]) {
                            VNPixelBufferObservation *pbo = (VNPixelBufferObservation *)obs;
                            CVPixelBufferRef pb = pbo.pixelBuffer;
                            NSLog(@"[VIRO_MONO_DEBUG]   VNPixelBufferObservation: %zux%zu, format=%u",
                                  CVPixelBufferGetWidth(pb), CVPixelBufferGetHeight(pb),
                                  (unsigned)CVPixelBufferGetPixelFormatType(pb));
                        }
                    }
                    NSLog(@"[VIRO_MONO_DEBUG] === End Results ===");
                }
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

    // Log model input/output descriptions for debugging
    NSLog(@"[VIRO_MONO_DEBUG] === Model Description ===");
    for (NSString *inputName in _model.modelDescription.inputDescriptionsByName) {
        MLFeatureDescription *desc = _model.modelDescription.inputDescriptionsByName[inputName];
        NSLog(@"[VIRO_MONO_DEBUG] Input '%@': type=%ld", inputName, (long)desc.type);
        if (desc.imageConstraint) {
            NSLog(@"[VIRO_MONO_DEBUG]   Image: %zux%zu, pixelFormat=%u",
                  desc.imageConstraint.pixelsWide,
                  desc.imageConstraint.pixelsHigh,
                  (unsigned)desc.imageConstraint.pixelFormatType);
        }
        if (desc.multiArrayConstraint) {
            NSLog(@"[VIRO_MONO_DEBUG]   MultiArray shape: %@, dataType=%ld",
                  desc.multiArrayConstraint.shape, (long)desc.multiArrayConstraint.dataType);
        }
    }
    for (NSString *outputName in _model.modelDescription.outputDescriptionsByName) {
        MLFeatureDescription *desc = _model.modelDescription.outputDescriptionsByName[outputName];
        NSLog(@"[VIRO_MONO_DEBUG] Output '%@': type=%ld", outputName, (long)desc.type);
        if (desc.multiArrayConstraint) {
            NSLog(@"[VIRO_MONO_DEBUG]   MultiArray shape: %@, dataType=%ld",
                  desc.multiArrayConstraint.shape, (long)desc.multiArrayConstraint.dataType);
        }
    }
    NSLog(@"[VIRO_MONO_DEBUG] === End Model Description ===");

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
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        return device != nil;
    }
    return false;
}

#pragma mark - Frame Processing

void VROMonocularDepthEstimator::update(const VROARFrame *frame) {
    if (!isAvailable()) {
        return;
    }

    // Skip if already processing or frame already queued to avoid ARFrame accumulation
    if (_isProcessing) {
        return;
    }

    // Thermal-adaptive rate limiting.
    // NSProcessInfoThermalState: Nominal=0, Fair=1, Serious=2, Critical=3
    // Throttle down as the device warms up to prevent sustained heat.
    double currentTime = VROTimeCurrentMillis();
    {
        int effectiveFPS = _targetFPS;
        if (@available(iOS 11.0, *)) {
            NSProcessInfoThermalState state = [NSProcessInfo processInfo].thermalState;
            if (state == NSProcessInfoThermalStateCritical) {
                effectiveFPS = 0;  // stop entirely — device needs to cool
            } else if (state == NSProcessInfoThermalStateSerious) {
                effectiveFPS = std::min(effectiveFPS, 2);
            } else if (state == NSProcessInfoThermalStateFair) {
                effectiveFPS = std::min(effectiveFPS, 3);
            }
            // Nominal: use full _targetFPS
        }
        if (effectiveFPS <= 0) {
            return;
        }
        double targetInterval = 1000.0 / effectiveFPS;
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
    bool shouldDispatch = false;
    {
        std::lock_guard<std::mutex> lock(_imageMutex);

        // Only queue a new frame if there isn't one already pending
        if (!_nextImage) {
            _nextImage = CVBufferRetain(image);
            _nextTransform = frame->getViewportToCameraImageTransform().invert();
            _nextOrientation = frameiOS->getImageOrientation();
            shouldDispatch = true;
            
            // DEBUG: Log input details
            static bool loggedInput = false;
            if (!loggedInput) {
                size_t w = CVPixelBufferGetWidth(image);
                size_t h = CVPixelBufferGetHeight(image);
                OSType type = CVPixelBufferGetPixelFormatType(image);
                // Correctly swap bytes for 4CC printing if needed, but simple cast is okay for debug
                // type is typically '420f' or similar
                NSLog(@"[VIRO_MONO_DEBUG] Input Image: %zux%zu, Format: %u", w, h, (unsigned int)type);
                loggedInput = true;
            }
            
        } else {
            if (kDebugMonoDepth) pinfo("VROMonocularDepthEstimator: Skipped frame (already queued)");
        }
    }

    // Only dispatch if we actually queued a new frame
    if (shouldDispatch) {
        // Update inference time NOW to prevent multiple dispatches
        _lastInferenceTime = currentTime;
        if (kDebugMonoDepth) NSLog(@"[Monocular Depth] Dispatching inference frame to background queue");
        if (kDebugMonoDepth) pinfo("Dispatching depth inference to background queue");
        
        std::weak_ptr<VROMonocularDepthEstimator> weak_self = shared_from_this();

        dispatch_async(_depthQueue, ^{
            std::shared_ptr<VROMonocularDepthEstimator> strong_self = weak_self.lock();
            if (strong_self) {
                strong_self->nextImage();
            }
        });
    }
}

void VROMonocularDepthEstimator::nextImage() {
    // Check if we're already processing
    bool expected = false;
    if (!_isProcessing.compare_exchange_strong(expected, true)) {
        // Already processing, skip this frame
        if (kDebugMonoDepth) pinfo("VROMonocularDepthEstimator: Already processing, skipping");
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
            if (kDebugMonoDepth) pinfo("VROMonocularDepthEstimator: No image queued, returning");
            return;
        }

        imageToProcess = _nextImage;
        _nextImage = nullptr;
        transform = _nextTransform;
        orientation = _nextOrientation;
        if (kDebugMonoDepth) pinfo("VROMonocularDepthEstimator: Got queued image, cleared queue");
    }

    // Store as processing image (we own this reference now)
    if (_processingImage) {
        CVBufferRelease(_processingImage);
        if (kDebugMonoDepth) NSLog(@"[Monocular Depth] Released previous processing image");
        if (kDebugMonoDepth) pinfo("Released previous depth processing image");
    }
    _processingImage = imageToProcess;

    // Run inference
    if (kDebugMonoDepth) NSLog(@"[Monocular Depth] Running CoreML inference on frame");
    if (kDebugMonoDepth) pinfo("Starting CoreML depth inference");
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
        // If the request failed, ensure we release the processing image to avoid retaining ARFrames
        if (_processingImage) {
            CVBufferRelease(_processingImage);
            _processingImage = nullptr;
            pinfo("VROMonocularDepthEstimator: Released processing image after error");
        }
    }

    // Update diagnostics
    double inferenceTime = VROTimeCurrentMillis() - startTime;
    updateDiagnostics(inferenceTime);

    // Mark as done processing
    _isProcessing = false;
    if (kDebugMonoDepth) pinfo("Depth inference complete in %.0fms", inferenceTime);

    // Check if there's another frame waiting and process it
    // IMPORTANT: Do NOT call nextImage() while holding _imageMutex to avoid deadlock
    // nextImage() will acquire the mutex, so we must be unlocked here.
    bool shouldTriggerNext = false;
    {
        std::lock_guard<std::mutex> lock(_imageMutex);
        if (_nextImage) {
            shouldTriggerNext = true;
        }
    }

    if (shouldTriggerNext) {
        if (kDebugMonoDepth) pinfo("Triggering processing for next queued frame");
        
        // Dispatch to self (on same serial queue) to process next frame
        // This avoids recursion and ensures we don't hold any locks
        std::weak_ptr<VROMonocularDepthEstimator> weak_self = shared_from_this();
        dispatch_async(_depthQueue, ^{
            std::shared_ptr<VROMonocularDepthEstimator> strong_self = weak_self.lock();
            if (strong_self) {
                strong_self->nextImage();
            }
        });
    }
}

void VROMonocularDepthEstimator::warmup() {
    if (!isAvailable()) {
        return;
    }

    std::weak_ptr<VROMonocularDepthEstimator> weak_self = shared_from_this();
    dispatch_async(_depthQueue, ^{
        std::shared_ptr<VROMonocularDepthEstimator> strong_self = weak_self.lock();
        if (!strong_self || !strong_self->isAvailable()) {
            return;
        }

        // Allocate a small black pixel buffer; Vision rescales it to the model input.
        const size_t w = 256, h = 256;
        CVPixelBufferRef buffer = NULL;
        NSDictionary *attrs = @{ (id)kCVPixelBufferIOSurfacePropertiesKey : @{} };
        CVReturn cvret = CVPixelBufferCreate(kCFAllocatorDefault, w, h,
                                             kCVPixelFormatType_32BGRA,
                                             (__bridge CFDictionaryRef)attrs, &buffer);
        if (cvret != kCVReturnSuccess || !buffer) {
            pwarn("VROMonocularDepthEstimator: warmup pixel buffer allocation failed");
            return;
        }
        CVPixelBufferLockBaseAddress(buffer, 0);
        void *base = CVPixelBufferGetBaseAddress(buffer);
        if (base) {
            memset(base, 0, CVPixelBufferGetBytesPerRow(buffer) * CVPixelBufferGetHeight(buffer));
        }
        CVPixelBufferUnlockBaseAddress(buffer, 0);

        // Use a private throwaway request (with an empty handler) so warmup does NOT
        // run processDepthOutput / overwrite the live depth buffers. ANE specialization
        // happens at the VNCoreMLModel level, so this still primes the real request.
        VNCoreMLRequest *warmRequest =
            [[VNCoreMLRequest alloc] initWithModel:strong_self->_coreMLModel
                                 completionHandler:^(VNRequest *req, NSError *err) { /* discard */ }];
        warmRequest.imageCropAndScaleOption = VNImageCropAndScaleOptionScaleFill;

        double startTime = VROTimeCurrentMillis();
        CIImage *ciImage = [[CIImage alloc] initWithCVPixelBuffer:buffer];
        VNImageRequestHandler *handler =
            [[VNImageRequestHandler alloc] initWithCIImage:ciImage
                                               orientation:kCGImagePropertyOrientationUp
                                                   options:@{}];
        NSError *error = nil;
        [handler performRequests:@[warmRequest] error:&error];
        CVBufferRelease(buffer);

        if (error) {
            pwarn("VROMonocularDepthEstimator: warmup inference error: %s",
                  [[error localizedDescription] UTF8String]);
        } else {
            pinfo("VROMonocularDepthEstimator: warmup inference complete in %.0fms",
                  VROTimeCurrentMillis() - startTime);
        }
    });
}

#pragma mark - Depth Output Processing

void VROMonocularDepthEstimator::processDepthOutput(VNCoreMLFeatureValueObservation *result,
                                                     VROMatrix4f transform) {
    if (!result || !result.featureValue) {
        // Always release processing image, even on error
        if (_processingImage) {
            CVBufferRelease(_processingImage);
            _processingImage = nullptr;
        }
        return;
    }

    MLMultiArray *depthArray = result.featureValue.multiArrayValue;
    if (!depthArray) {
        pwarn("VROMonocularDepthEstimator: No depth array in result");
        // Always release processing image, even on error
        if (_processingImage) {
            CVBufferRelease(_processingImage);
            _processingImage = nullptr;
        }
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
        // Always release processing image, even on error
        if (_processingImage) {
            CVBufferRelease(_processingImage);
            _processingImage = nullptr;
        }
        return;
    }

    // Resize buffer if needed
    size_t bufferSize = width * height;
    if (_depthBuffer.size() != bufferSize) {
        _depthBuffer.resize(bufferSize);
        _previousDepthBuffer.clear(); // Reset temporal filter
    }
    
    // Log info about the array type and strides once
    static bool loggedArrayInfo = false;
    if (!loggedArrayInfo) {
        NSLog(@"[VIRO_MONO] MLMultiArray DataType: %ld (1=Double, 2=Float32, 3=Float16)", (long)depthArray.dataType);
        NSLog(@"[VIRO_MONO] MLMultiArray Strides: %@", depthArray.strides);
        loggedArrayInfo = true;
    }

    // Copy and process depth data for Depth Anything V2
    // Model outputs metric depth directly in meters.
    // Apply a calibration scale factor to align with ARKit distances.

    float *rawDepthFloat = nullptr;
    double *rawDepthDouble = nullptr;
    uint16_t *rawDepthFloat16 = nullptr;

    if (depthArray.dataType == MLMultiArrayDataTypeFloat32) {
        rawDepthFloat = (float *)depthArray.dataPointer;
    } else if (depthArray.dataType == MLMultiArrayDataTypeDouble) {
        rawDepthDouble = (double *)depthArray.dataPointer;
    } else if (depthArray.dataType == 65568 /* MLMultiArrayDataTypeFloat16 */) {
        rawDepthFloat16 = (uint16_t *)depthArray.dataPointer;
    }

    // Determine strides based on shape
    NSInteger strideY = width;
    NSInteger strideX = 1;

    if (depthArray.strides.count >= 2) {
        if (shape.count == 3 && depthArray.strides.count >= 3) {
            strideY = [depthArray.strides[1] integerValue];
            strideX = [depthArray.strides[2] integerValue];
        } else {
            strideY = [depthArray.strides[depthArray.strides.count - 2] integerValue];
            strideX = [depthArray.strides[depthArray.strides.count - 1] integerValue];
        }

        if (strideY < width || strideY > width * 2) {
             NSLog(@"[VIRO_MONO] Warning: Unusual StrideY: %ld (Width: %d). Using packed stride.", (long)strideY, width);
             strideY = width;
             strideX = 1;
        }
    }

    float minRaw = FLT_MAX;
    float maxRaw = -FLT_MAX;
    const float effectiveScale = (_calibrationMode == VROMonocularDepthCalibration::None)
                                 ? 1.0f : _depthScaleFactor;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            NSInteger rawIndex = y * strideY + x * strideX;
            size_t packedIndex = y * width + x;

            float rawVal = 0.0f;
            if (rawDepthFloat) {
                rawVal = rawDepthFloat[rawIndex];
            } else if (rawDepthFloat16) {
                rawVal = float16ToFloat32(rawDepthFloat16[rawIndex]);
            } else if (rawDepthDouble) {
                rawVal = (float)rawDepthDouble[rawIndex];
            } else {
                @try {
                    NSArray *key = (shape.count == 3) ? @[@0, @(y), @(x)] : @[@(y), @(x)];
                    rawVal = [[depthArray objectForKeyedSubscript:key] floatValue];
                } @catch (NSException *e) {
                    rawVal = 0.0f;
                }
            }

            if (rawVal < minRaw) minRaw = rawVal;
            if (rawVal > maxRaw) maxRaw = rawVal;

            if (rawVal < 0.0f) rawVal = 0.0f;
            rawVal *= effectiveScale;

            _depthBuffer[packedIndex] = rawVal;
        }
    }

    // Log depth stats occasionally
    static int logCounter = 0;
    if (logCounter++ % 30 == 0) {
        NSLog(@"[VIRO_MONO] DepthAnything Stats: Min=%.4fm, Max=%.4fm (scaled: %.4f-%.4f)",
              minRaw, maxRaw, minRaw * _depthScaleFactor, maxRaw * _depthScaleFactor);
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

    // CRITICAL: Release the processing image now that we're done with it
    // This prevents ARFrame retention that freezes the camera
    if (_processingImage) {
        CVBufferRelease(_processingImage);
        _processingImage = nullptr;
        NSLog(@"[VIRO_MONO] Released processing image after depth output");
        pinfo("VROMonocularDepthEstimator: Released processing image");
    }
}

void VROMonocularDepthEstimator::applyTemporalFilter(float *depthData, int width, int height) {
    size_t bufferSize = width * height;
    _confidenceBuffer.resize(bufferSize);

    // First frame or size change: no history yet
    if (_previousDepthBuffer.empty() || _previousDepthBuffer.size() != bufferSize) {
        _previousDepthBuffer.assign(depthData, depthData + bufferSize);
        std::fill(_confidenceBuffer.begin(), _confidenceBuffer.end(), 0.5f);
        return;
    }

    const float alpha = _temporalFilterAlpha;
    const float edgeThreshold = 0.3f; // 30 cm discontinuity threshold

    for (size_t i = 0; i < bufferSize; i++) {
        float prevDepth = _previousDepthBuffer[i];
        float currDepth = depthData[i];
        float diff = std::abs(currDepth - prevDepth);

        // Synthesize per-pixel confidence from temporal stability
        if (currDepth <= 0.0f) {
            _confidenceBuffer[i] = 0.0f;           // invalid depth
        } else if (prevDepth <= 0.0f) {
            _confidenceBuffer[i] = 0.3f;           // no prior history for this pixel
        } else {
            // Linear decay: 1.0 at diff=0, 0.0 at diff=edgeThreshold
            _confidenceBuffer[i] = std::max(0.0f, 1.0f - diff / edgeThreshold);
        }

        // Apply EMA only away from discontinuities
        if (diff < edgeThreshold && prevDepth > 0 && currDepth > 0) {
            depthData[i] = prevDepth + alpha * (currDepth - prevDepth);
        }
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
        // Same dimensions: write to staging buffer; render thread flushes via glTexSubImage2D
        _stagingDepthBuffer.assign(depthData, depthData + width * height);
        _stagingDirty.store(true);
        return;
    }
}

VROMatrix4f VROMonocularDepthEstimator::computeDepthTextureTransform(VROMatrix4f imageTransform,
                                                                      int imageWidth, int imageHeight,
                                                                      int depthWidth, int depthHeight) {
    // For monocular depth, the depth texture maps directly to the camera view.
    // Vision's VNCoreMLRequest already handled crop/scale to feed the model,
    // so the depth output covers the same field of view as the camera image.
    //
    // The occlusion shader computes screen UV as:
    //   screenUV = gl_FragCoord.xy / viewport_size;
    //   screenUV.y = 1.0 - screenUV.y;  // flip Y to top-left origin
    //   depthUV = (transform * vec4(screenUV, 0, 1)).xy;
    //
    // With identity transform, depthUV = screenUV, which directly samples
    // the depth texture in the correct orientation.
    return VROMatrix4f::identity();
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

const float* VROMonocularDepthEstimator::getDepthBufferData() const {
    std::lock_guard<std::mutex> lock(_depthMutex);
    if (_depthBuffer.empty()) {
        return nullptr;
    }
    return _depthBuffer.data();
}

bool VROMonocularDepthEstimator::snapshotDepthBuffers(std::vector<float> &outDepth,
                                                      std::vector<float> &outConfidence,
                                                      int &outWidth, int &outHeight) const {
    std::lock_guard<std::mutex> lock(_depthMutex);
    if (_depthBuffer.empty() || _depthWidth <= 0 || _depthHeight <= 0) return false;
    outDepth = _depthBuffer;
    outConfidence = _confidenceBuffer;
    outWidth = _depthWidth;
    outHeight = _depthHeight;
    return true;
}

#pragma mark - Configuration

void VROMonocularDepthEstimator::setScaleFactor(float scale) {
    _depthScaleFactor = scale;
    _calibrationMode = VROMonocularDepthCalibration::Manual;
}

void VROMonocularDepthEstimator::setCalibrationMode(VROMonocularDepthCalibration mode) {
    if (mode == VROMonocularDepthCalibration::LiDARReference) {
        NSLog(@"[ViroMono] LiDARReference calibration not yet implemented — falling back to Manual");
        _calibrationMode = VROMonocularDepthCalibration::Manual;
        return;
    }
    _calibrationMode = mode;
    if (mode == VROMonocularDepthCalibration::None) {
        _depthScaleFactor = 1.0f;
    }
}

VROMonocularDepthCalibration VROMonocularDepthEstimator::getCalibrationMode() const {
    return _calibrationMode;
}

void VROMonocularDepthEstimator::setHitTestConfidenceThreshold(float threshold) {
    _hitTestConfidenceThreshold = std::max(0.0f, std::min(1.0f, threshold));
}

float VROMonocularDepthEstimator::getHitTestConfidenceThreshold() const {
    return _hitTestConfidenceThreshold;
}

float VROMonocularDepthEstimator::sampleConfidenceAtDepthUV(float u, float v) const {
    std::lock_guard<std::mutex> lock(_depthMutex);
    if (_confidenceBuffer.empty() || _depthWidth <= 0 || _depthHeight <= 0) {
        return -1.0f;
    }

    float px = u * (_depthWidth - 1);
    float py = v * (_depthHeight - 1);
    int x0 = std::max(0, std::min((int)px, _depthWidth - 1));
    int y0 = std::max(0, std::min((int)py, _depthHeight - 1));
    int x1 = std::min(x0 + 1, _depthWidth - 1);
    int y1 = std::min(y0 + 1, _depthHeight - 1);
    float fx = px - (int)px;
    float fy = py - (int)py;

    float c00 = _confidenceBuffer[y0 * _depthWidth + x0];
    float c10 = _confidenceBuffer[y0 * _depthWidth + x1];
    float c01 = _confidenceBuffer[y1 * _depthWidth + x0];
    float c11 = _confidenceBuffer[y1 * _depthWidth + x1];

    return (1.0f - fx) * (1.0f - fy) * c00
         + fx          * (1.0f - fy) * c10
         + (1.0f - fx) * fy          * c01
         + fx          * fy          * c11;
}

void VROMonocularDepthEstimator::flushPendingDepthUpdate() {
    if (!_stagingDirty.load()) return;
    std::lock_guard<std::mutex> lock(_depthMutex);
    if (!_stagingDirty.load() || !_currentDepthTexture || _stagingDepthBuffer.empty()) return;

    std::shared_ptr<VRODriver> driver = _driver.lock();
    if (!driver) return;

    VROTextureSubstrate *sub = _currentDepthTexture->getSubstrate(0, driver, false);
    if (sub) {
        sub->updateR32FData(_stagingDepthBuffer.data(), _depthWidth, _depthHeight);
    }
    _stagingDirty.store(false);
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

        pinfo("VROMonocularDepthEstimator: avg latency %.2f ms, fps %.2f, depth=%dx%d",
              _averageLatencyMs, _currentFPS, _depthWidth, _depthHeight);

        _frameCount = 0;
        _latencyAccumulator = 0;
    }
}

#endif // __IPHONE_OS_VERSION_MAX_ALLOWED >= 140000
