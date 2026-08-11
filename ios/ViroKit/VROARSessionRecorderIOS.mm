//
//  VROARSessionRecorderIOS.mm
//  ViroKit
//
//  See VROARSessionRecorderIOS.h.
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VROARSessionRecorderIOS.h"
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000

#include "VROLog.h"
#include "VROMatrix4f.h"
#include "VROQuaternion.h"
#include "VROConvert.h"
#include "VROCameraTexture.h" // VROCameraOrientation

#import <Foundation/Foundation.h>

// Matches ARFrame.capturedImage's documented format. If a future OS/device
// combination hands us something else, recordFrame() logs once and skips
// video for that session rather than crash on a format AVAssetWriter can't
// take as configured.
static const OSType kExpectedPixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;

VROARSessionRecorderIOS::VROARSessionRecorderIOS() :
    _status(VROARRecordingStatus::None),
    _startTimestamp(0),
    _videoWriter(nil),
    _videoWriterInput(nil),
    _videoAdaptor(nil),
    _loggedPixelFormatMismatch(false),
    _motionManager(nil),
    _imuQueue(nil),
    _wroteHeader(false) {
}

VROARSessionRecorderIOS::~VROARSessionRecorderIOS() {
    stop();
}

bool VROARSessionRecorderIOS::start(const VROARRecordingConfig &config,
                                     std::function<void()> onSuccess,
                                     std::function<void(std::string error)> onFailure) {
    if (_status == VROARRecordingStatus::Recording) {
        if (onFailure) onFailure("Already recording");
        return false;
    }

    NSString *dir = [NSString stringWithUTF8String:config.outputDir.c_str()];
    NSError *dirError = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                    withIntermediateDirectories:YES
                                                     attributes:nil
                                                          error:&dirError]) {
        if (onFailure) onFailure(std::string("Could not create output directory: ") +
                                  [dirError.localizedDescription UTF8String]);
        return false;
    }

    _outputDir = config.outputDir;
    _startTimestamp = 0;
    _wroteHeader = false;
    _loggedPixelFormatMismatch = false;

    // video.mp4 itself is created lazily, on the first recordFrame(), once we
    // know the camera's actual image resolution (AVAssetWriterInput's output
    // settings are fixed at creation time).
    _videoWriter = nil;
    _videoWriterInput = nil;
    _videoAdaptor = nil;

    std::string sidecarPath = _outputDir + "/session.jsonl";
    _sidecar.open(sidecarPath, std::ios::out | std::ios::trunc);
    if (!_sidecar.is_open()) {
        if (onFailure) onFailure("Could not open session.jsonl for writing at " + sidecarPath);
        return false;
    }

    // Raw IMU: CMMotionManager's un-fused accelerometer/gyro APIs (NOT
    // startDeviceMotionUpdates, which is sensor fusion). Deliberately a tap
    // independent of ARKit — see plan §2.1.
    _motionManager = [[CMMotionManager alloc] init];
    if (!_motionManager.isAccelerometerAvailable || !_motionManager.isGyroAvailable) {
        closeSidecar();
        if (onFailure) onFailure("Raw accelerometer/gyro not available on this device");
        return false;
    }
    _imuQueue = [[NSOperationQueue alloc] init];
    _imuQueue.maxConcurrentOperationCount = 1;
    _imuQueue.name = @"com.viro.ar.recorder.imu";

    _motionManager.accelerometerUpdateInterval = 1.0 / 100.0;
    _motionManager.gyroUpdateInterval = 1.0 / 100.0;

    __block CMAcceleration lastAccel = {0, 0, 0};
    __block BOOL haveAccel = NO;
    __block CMRotationRate lastGyro = {0, 0, 0};
    __block BOOL haveGyro = NO;

    // CoreMotion delivers accel/gyro as two independent streams with their
    // own timestamps — there is no synced "raw IMU sample" API on iOS. We
    // pair them by writing one `imu` line per accelerometer sample, using
    // whatever gyro sample most recently arrived (a standard "zip-latest"
    // pairing; the two streams run at the same configured rate so the skew
    // is at most one sample period).
    [_motionManager startGyroUpdatesToQueue:_imuQueue withHandler:^(CMGyroData *data, NSError *error) {
        if (data) {
            lastGyro = data.rotationRate;
            haveGyro = YES;
        }
    }];
    [_motionManager startAccelerometerUpdatesToQueue:_imuQueue withHandler:^(CMAccelerometerData *data, NSError *error) {
        if (!data) return;
        lastAccel = data.acceleration;
        haveAccel = YES;
        if (!haveGyro) return; // wait for at least one gyro sample so the line isn't half-zero
        this->writeImuLine(data.timestamp, lastAccel.x, lastAccel.y, lastAccel.z,
                            lastGyro.x, lastGyro.y, lastGyro.z);
    }];

    // Gravity for the `pose` line's ground-truth reference — this is exactly
    // what CMDeviceMotion's fusion already separates out; running it
    // alongside the raw taps above is independent and does not affect them.
    if (_motionManager.isDeviceMotionAvailable) {
        [_motionManager startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXArbitraryZVertical];
    }

    _status = VROARRecordingStatus::Recording;
    if (onSuccess) onSuccess();
    return true;
}

void VROARSessionRecorderIOS::stop() {
    if (_status != VROARRecordingStatus::Recording && _status != VROARRecordingStatus::IOError) {
        return;
    }

    if (_motionManager) {
        [_motionManager stopAccelerometerUpdates];
        [_motionManager stopGyroUpdates];
        [_motionManager stopDeviceMotionUpdates];
        _motionManager = nil;
    }
    _imuQueue = nil;

    if (_videoWriter && _videoWriter.status == AVAssetWriterStatusWriting) {
        [_videoWriterInput markAsFinished];
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        [_videoWriter finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(sema);
        }];
        // Bounded wait so a stuck encoder can't hang the caller forever —
        // stop() is expected to return promptly (e.g. app backgrounding).
        dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5.0 * NSEC_PER_SEC)));
    }
    _videoWriter = nil;
    _videoWriterInput = nil;
    _videoAdaptor = nil;

    closeSidecar();
    _status = VROARRecordingStatus::None;
}

VROARRecordingStatus VROARSessionRecorderIOS::getStatus() const {
    return _status;
}

void VROARSessionRecorderIOS::closeSidecar() {
    std::lock_guard<std::mutex> lock(_sidecarMutex);
    if (_sidecar.is_open()) {
        _sidecar.close();
    }
}

void VROARSessionRecorderIOS::writeHeaderIfNeeded(ARFrame *frame) {
    if (_wroteHeader) return;
    _wroteHeader = true;

    matrix_float3x3 intrinsics = frame.camera.intrinsics;
    CGSize res = frame.camera.imageResolution;

    char buf[512];
    // Extrinsics default to identity — a safe fallback per the plan; iOS
    // does not expose a per-device IMU/camera calibration to do better here.
    snprintf(buf, sizeof(buf),
        "{\"type\":\"header\",\"intrinsics\":{\"fx\":%.4f,\"fy\":%.4f,\"cx\":%.4f,\"cy\":%.4f,\"width\":%d,\"height\":%d},"
        "\"extrinsics\":{\"q_imu_cam\":[0,0,0,1],\"p_imu_cam\":[0,0,0],\"time_offset\":0.0}}",
        intrinsics.columns[0][0], intrinsics.columns[1][1],
        intrinsics.columns[2][0], intrinsics.columns[2][1],
        (int)res.width, (int)res.height);

    std::lock_guard<std::mutex> lock(_sidecarMutex);
    _sidecar << buf << "\n";
    _sidecar.flush();
}

void VROARSessionRecorderIOS::writeImuLine(double tSec, double ax, double ay, double az,
                                            double gx, double gy, double gz) {
    if (_status != VROARRecordingStatus::Recording) return;
    // CMAccelerometerData/.timestamp is seconds since device boot (matches
    // ARFrame.timestamp's clock), converted to the sidecar's nanoseconds.
    int64_t tNs = (int64_t)(tSec * 1e9);
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"imu\",\"t\":%lld,\"accel\":[%.6f,%.6f,%.6f],\"gyro\":[%.6f,%.6f,%.6f]}",
        (long long)tNs, ax, ay, az, gx, gy, gz);

    std::lock_guard<std::mutex> lock(_sidecarMutex);
    if (_sidecar.is_open()) {
        _sidecar << buf << "\n";
    }
}

void VROARSessionRecorderIOS::writePoseLine(ARFrame *frame) {
    VROMatrix4f transform = VROConvert::toMatrix4f(frame.camera.transform);
    VROMatrix4f rotationOnly = transform;
    rotationOnly[12] = 0; rotationOnly[13] = 0; rotationOnly[14] = 0;
    VROQuaternion q(rotationOnly);
    const float *m = transform.getArray();

    CMAcceleration gravity = {0, 0, 0};
    if (_motionManager && _motionManager.deviceMotion) {
        gravity = _motionManager.deviceMotion.gravity;
    }

    int64_t tNs = (int64_t)(frame.timestamp * 1e9);
    char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"pose\",\"t\":%lld,\"orientation\":[%.6f,%.6f,%.6f,%.6f],"
        "\"position\":[%.6f,%.6f,%.6f],\"gravity\":[%.6f,%.6f,%.6f]}",
        (long long)tNs, q.X, q.Y, q.Z, q.W, m[12], m[13], m[14],
        gravity.x, gravity.y, gravity.z);

    std::lock_guard<std::mutex> lock(_sidecarMutex);
    if (_sidecar.is_open()) {
        _sidecar << buf << "\n";
    }
}

void VROARSessionRecorderIOS::recordFrame(ARFrame *frame) {
    if (_status != VROARRecordingStatus::Recording || !frame) {
        return;
    }
    if (_startTimestamp == 0) {
        _startTimestamp = frame.timestamp;
    }

    writeHeaderIfNeeded(frame);

    CVPixelBufferRef pixelBuffer = frame.capturedImage;
    if (pixelBuffer) {
        if (CVPixelBufferGetPixelFormatType(pixelBuffer) != kExpectedPixelFormat) {
            if (!_loggedPixelFormatMismatch) {
                perr("VROARSessionRecorderIOS: capturedImage format changed unexpectedly, "
                     "skipping video for this session (session.jsonl still recorded)");
                _loggedPixelFormatMismatch = true;
            }
        } else {
            // Lazily create the writer once we know the real frame size —
            // AVAssetWriterInput's outputSettings are fixed at creation.
            if (!_videoWriter) {
                size_t width = CVPixelBufferGetWidth(pixelBuffer);
                size_t height = CVPixelBufferGetHeight(pixelBuffer);
                NSString *videoPath = [NSString stringWithUTF8String:(_outputDir + "/video.mp4").c_str()];
                [[NSFileManager defaultManager] removeItemAtPath:videoPath error:nil];
                NSURL *videoURL = [NSURL fileURLWithPath:videoPath];

                NSError *writerError = nil;
                _videoWriter = [[AVAssetWriter alloc] initWithURL:videoURL
                                                          fileType:AVFileTypeMPEG4
                                                             error:&writerError];
                if (!_videoWriter) {
                    perr("VROARSessionRecorderIOS: AVAssetWriter setup failed: %s",
                         writerError ? [writerError.localizedDescription UTF8String] : "unknown error");
                    _status = VROARRecordingStatus::IOError;
                    return;
                }

                NSDictionary *videoSettings = @{
                    AVVideoCodecKey: AVVideoCodecTypeH264,
                    AVVideoWidthKey: @(width),
                    AVVideoHeightKey: @(height),
                    AVVideoCompressionPropertiesKey: @{ AVVideoAverageBitRateKey: @(11000000) }
                };
                _videoWriterInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                        outputSettings:videoSettings];
                _videoWriterInput.expectsMediaDataInRealTime = YES;

                NSDictionary *pixelBufferAttributes = @{
                    (id)kCVPixelBufferPixelFormatTypeKey: @(kExpectedPixelFormat),
                    (id)kCVPixelBufferWidthKey: @(width),
                    (id)kCVPixelBufferHeightKey: @(height)
                };
                _videoAdaptor = [AVAssetWriterInputPixelBufferAdaptor
                    assetWriterInputPixelBufferAdaptorWithAssetWriterInput:_videoWriterInput
                    sourcePixelBufferAttributes:pixelBufferAttributes];

                [_videoWriter addInput:_videoWriterInput];
                [_videoWriter startWriting];
                [_videoWriter startSessionAtSourceTime:kCMTimeZero];
            }

            if (_videoWriterInput.readyForMoreMediaData) {
                double relativeSec = frame.timestamp - _startTimestamp;
                CMTime pts = CMTimeMakeWithSeconds(relativeSec, 1000000);
                if (![_videoAdaptor appendPixelBuffer:pixelBuffer withPresentationTime:pts]) {
                    perr("VROARSessionRecorderIOS: appendPixelBuffer failed (writer status %ld)",
                         (long)_videoWriter.status);
                }
            }
            // If not yet ready for more data, this frame's video is dropped —
            // the pose line below is still written every frame regardless
            // (frames stay associated with poses by array position on the
            // consumer side per the plan, so a dropped video frame here
            // would desync that pairing in a real encoder-backpressure case;
            // expectsMediaDataInRealTime plus H.264's hardware encoder easily
            // keeping up with a camera feed makes this a rare edge, not
            // absent — flagged as a known limitation, not silently correct).
        }
    }

    writePoseLine(frame);
}

#endif /* __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000 */
