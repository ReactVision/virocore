//
//  VROMetalFrameTimer.mm
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VROMetalFrameTimer.h"
#if VRO_METAL

#include "VROLog.h"
#include <algorithm>
#include <mach/mach_time.h>

// Two samples per pass (start and end), so this caps the number of measured passes per frame.
// The renderer's busiest configuration is one shadow pass per light, the HDR pass, a handful
// of blur passes and the display pass — comfortably inside this.
static const int kVROMaxTimedPassesPerFrame = 32;

static double VROHostSeconds() {
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    const uint64_t ticks = mach_absolute_time();
    return (double)ticks * (double)timebase.numer / (double)timebase.denom / 1.0e9;
}

// ── Stats ────────────────────────────────────────────────────────────────────

double VROMetalFrameTimer::Stats::min() const {
    return samples.empty() ? 0.0 : *std::min_element(samples.begin(), samples.end());
}

double VROMetalFrameTimer::Stats::max() const {
    return samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end());
}

double VROMetalFrameTimer::Stats::median() const {
    if (samples.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

double VROMetalFrameTimer::Stats::p95() const {
    if (samples.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    size_t index = (size_t)(sorted.size() * 0.95);
    if (index >= sorted.size()) {
        index = sorted.size() - 1;
    }
    return sorted[index];
}

// ── Construction ─────────────────────────────────────────────────────────────

VROMetalFrameTimer::VROMetalFrameTimer(id <MTLDevice> device, int windowFrames,
                                       int reportEveryFrames) :
    _device(device),
    _sampleBuffer(nil),
    _commandBuffer(nil),
    _windowFrames(windowFrames),
    _reportEveryFrames(reportEveryFrames),
    _frameCount(0),
    _sampleCursor(0),
    _sampleCapacity(kVROMaxTimedPassesPerFrame * 2),
    _frameStart(0.0),
    _gpuTicksToMs(0.0) {

    // Per-pass timing needs a timestamp counter set that can be sampled at stage
    // boundaries. Both parts have to be checked: a device can expose the counter set and
    // still refuse stage-boundary sampling, which is the case on the Simulator.
    id <MTLCounterSet> timestampSet = nil;
    for (id <MTLCounterSet> set in [device counterSets]) {
        if ([[set name] isEqualToString:MTLCommonCounterSetTimestamp]) {
            timestampSet = set;
            break;
        }
    }

    if (timestampSet && [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
        MTLCounterSampleBufferDescriptor *descriptor = [MTLCounterSampleBufferDescriptor new];
        descriptor.counterSet = timestampSet;
        descriptor.sampleCount = _sampleCapacity;
        descriptor.storageMode = MTLStorageModeShared;

        NSError *error = nil;
        _sampleBuffer = [device newCounterSampleBufferWithDescriptor:descriptor error:&error];
        [descriptor release];

        if (!_sampleBuffer) {
            pinfo("VROMetalFrameTimer: no counter sample buffer (%s) — per-pass GPU timing off",
                  error ? [[error localizedDescription] UTF8String] : "unknown");
        }
    } else {
        pinfo("VROMetalFrameTimer: stage-boundary counter sampling unavailable; CPU and total GPU time only (expected in the Simulator)");
    }

    calibrateTimestampDomain();
}

VROMetalFrameTimer::~VROMetalFrameTimer() {
    [_sampleBuffer release];
}

void VROMetalFrameTimer::calibrateTimestampDomain() {
    // GPU timestamps come back in an opaque tick domain. sampleTimestamps hands back a
    // matched CPU/GPU pair, and CPU timestamps are in the mach_absolute_time domain, so the
    // ratio between two spaced-out pairs converts GPU ticks to real time.
    if (!_device || ![_device respondsToSelector:@selector(sampleTimestamps:gpuTimestamp:)]) {
        _gpuTicksToMs = 0.0;
        return;
    }
    MTLTimestamp cpuA = 0, gpuA = 0, cpuB = 0, gpuB = 0;
    [_device sampleTimestamps:&cpuA gpuTimestamp:&gpuA];
    // A short spin rather than a sleep: this runs once, at construction.
    const double until = VROHostSeconds() + 0.002;
    while (VROHostSeconds() < until) { }
    [_device sampleTimestamps:&cpuB gpuTimestamp:&gpuB];

    if (gpuB <= gpuA || cpuB <= cpuA) {
        _gpuTicksToMs = 0.0;
        return;
    }
    // Both timestamps come back in NANOSECONDS — measured on an Apple Vision Pro, where a
    // 2 ms window gave cpuDelta = gpuDelta = 2,000,041 while mach_absolute_time advanced by
    // 48,001 ticks over the same window (timebase 125/3, i.e. 41.667 ns per tick).
    //
    // The previous version treated the CPU value as mach ticks and converted it with the
    // timebase, which multiplied every per-pass duration by 41.667. That is how the report
    // came to claim a 131 ms display pass inside a 7.64 ms frame: precise, well-formatted,
    // and wrong by a factor no one would guess from looking at it. It only surfaced when the
    // per-pass figures were checked against the frame total, which comes from a different
    // source (GPUStartTime/GPUEndTime) and was correct all along.
    //
    // The ratio is kept rather than hardcoding 1e-6: if a device ever reports the GPU clock
    // in a domain of its own, this still resolves it, and cpuDelta stays in the units the
    // API documents.
    _gpuTicksToMs = ((double)(cpuB - cpuA) / (double)(gpuB - gpuA)) / 1.0e6;
}

// ── Frame lifecycle ──────────────────────────────────────────────────────────

void VROMetalFrameTimer::beginFrame(id <MTLCommandBuffer> commandBuffer) {
    _commandBuffer = commandBuffer;
    _sampleCursor = 0;
    _frameStart = VROHostSeconds();
    _currentFrame = std::make_shared<FrameRecord>();

    // The handler must be installed now: Metal asserts outright if one is added after the
    // buffer has been committed, and the render loop commits before the frame is closed out.
    // GPUStartTime/GPUEndTime are only valid once it has completed, so the read happens here.
    std::shared_ptr<FrameRecord> record = _currentFrame;
    id <MTLCounterSampleBuffer> sampleBuffer = _sampleBuffer;
    const double ticksToMs = _gpuTicksToMs;
    const int capacity = _sampleCapacity;
    VROMetalFrameTimer *timer = this;   // outlives the render loop it belongs to

    [commandBuffer addCompletedHandler:^(id <MTLCommandBuffer> buffer) {
        const double gpuMs = (buffer.GPUEndTime - buffer.GPUStartTime) * 1000.0;
        if (gpuMs > 0.0) {
            timer->_gpuTotal.add(gpuMs);
        }
        if (!sampleBuffer || record->passes.empty() || ticksToMs <= 0.0) {
            return;
        }
        NSData *data = [sampleBuffer resolveCounterRange:NSMakeRange(0, capacity)];
        if (!data) {
            return;
        }
        const MTLCounterResultTimestamp *timestamps =
            (const MTLCounterResultTimestamp *)[data bytes];
        const size_t count = [data length] / sizeof(MTLCounterResultTimestamp);

        for (const PendingPass &pass : record->passes) {
            if ((size_t)pass.endIndex >= count) {
                continue;
            }
            const MTLTimestamp start = timestamps[pass.startIndex].timestamp;
            const MTLTimestamp end   = timestamps[pass.endIndex].timestamp;
            // A pass the GPU skipped entirely reports zero; that is not a measurement.
            if (end <= start) {
                continue;
            }
            timer->_passStats[pass.label].add((double)(end - start) * ticksToMs);
        }
    }];
}

bool VROMetalFrameTimer::beginPass(MTLRenderPassDescriptor *descriptor, const std::string &label) {
    if (!_sampleBuffer || !descriptor) {
        return false;
    }
    if (_sampleCursor + 2 > _sampleCapacity) {
        return false;
    }
    const int startIndex = _sampleCursor;
    const int endIndex = _sampleCursor + 1;
    _sampleCursor += 2;

    MTLRenderPassSampleBufferAttachmentDescriptor *attachment =
        descriptor.sampleBufferAttachments[0];
    attachment.sampleBuffer = _sampleBuffer;
    attachment.startOfVertexSampleIndex = startIndex;
    attachment.endOfFragmentSampleIndex = endIndex;

    if (_currentFrame) {
        _currentFrame->passes.push_back({ label, startIndex, endIndex });
    }
    return true;
}

void VROMetalFrameTimer::endFrame() {
    if (!_commandBuffer) {
        return;
    }
    _cpuFrame.add((VROHostSeconds() - _frameStart) * 1000.0);

    _commandBuffer = nil;
    _currentFrame = nullptr;
    _frameCount++;

    if (_reportEveryFrames > 0 && _frameCount % _reportEveryFrames == 0) {
        report();
    }
}

void VROMetalFrameTimer::beginPhase(const std::string &label) {
    _phaseStarts[label] = VROHostSeconds();
}

void VROMetalFrameTimer::endPhase(const std::string &label) {
    auto it = _phaseStarts.find(label);
    if (it == _phaseStarts.end()) {
        return;
    }
    _phaseStats[label].add((VROHostSeconds() - it->second) * 1000.0);
    _phaseStarts.erase(it);
}

// ── Reporting ────────────────────────────────────────────────────────────────

void VROMetalFrameTimer::report() {
    // 90 Hz is the target cadence on Vision Pro, so 11.1 ms is the budget the numbers below
    // have to fit inside. Reported as a distribution because a mean hides the spikes that
    // actually drop frames.
    const double budgetMs = 1000.0 / 90.0;

    pinfo("── frame timing over %d frames (90 Hz budget = %.2f ms) ──",
          (int)std::max(_cpuFrame.samples.size(), _gpuTotal.samples.size()), budgetMs);

    auto line = [](const char *label, const Stats &stats) {
        if (stats.empty()) {
            return;
        }
        pinfo("  %-22s min %6.2f  med %6.2f  p95 %6.2f  max %6.2f ms",
              label, stats.min(), stats.median(), stats.p95(), stats.max());
    };

    line("GPU total", _gpuTotal);
    line("CPU frame", _cpuFrame);
    for (const auto &entry : _phaseStats) {
        line(("CPU " + entry.first).c_str(), entry.second);
    }

    if (!_gpuTotal.empty()) {
        const double p95 = _gpuTotal.p95();
        pinfo("  verdict: GPU p95 %.2f ms is %.0f%% of the 90 Hz budget%s",
              p95, 100.0 * p95 / budgetMs,
              p95 > budgetMs ? " — OVER" : "");
    }

    if (supportsPassTiming()) {
        for (const auto &entry : _passStats) {
            line(("GPU " + entry.first).c_str(), entry.second);
        }
    } else {
        // Kept on one line: pinfo stringifies its argument, so a split literal prints as
        // two separate quoted strings.
        pinfo("  per-pass GPU timing unavailable here; attribute cost by toggling enableShadows / enableHDR / enableBloom / enablePBR");
    }

    // Start a fresh window so each report describes a distinct stretch of time rather than
    // being progressively diluted by everything measured since launch.
    _gpuTotal.samples.clear();
    _cpuFrame.samples.clear();
    for (auto &entry : _passStats)  { entry.second.samples.clear(); }
    for (auto &entry : _phaseStats) { entry.second.samples.clear(); }
}

#endif  // VRO_METAL
