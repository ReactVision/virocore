//
//  VROMetalFrameTimer.h
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Frame cost measurement for the visionOS render loop.
//
//  Exists because the M4 budget is a number nobody has: 90 Hz means 11.1 ms per frame, and
//  shadows, HDR, bloom, PBR and IBL were all added and validated in the Simulator, where GPU
//  timings mean nothing. This produces the numbers on the first device run instead of turning
//  that session into an instrumentation exercise.
//
//  Three levels of detail, degrading gracefully:
//
//    1. CPU wall time per frame phase — always available.
//    2. Total GPU time per frame, from the command buffer's GPUStartTime/GPUEndTime —
//       always available, and the number to compare against 11.1 ms.
//    3. Per-pass GPU time, from timestamp counters sampled at each render pass's stage
//       boundaries. This is the one that says *what* to cut, and it needs
//       MTLCounterSetTimestamp with stage-boundary sampling. Where that is unavailable
//       (notably the Simulator) levels 1 and 2 still work.
//
//  Statistics are reported as min / median / p95 / max over a rolling window rather than a
//  running average: a mean hides exactly the intermittent spikes that break a frame budget.

#ifndef VROMetalFrameTimer_h
#define VROMetalFrameTimer_h

#include "VRODefines.h"
#if VRO_METAL

#include <Metal/Metal.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class VROMetalFrameTimer {
public:

    /*
     windowFrames — how many frames each report covers. reportEveryFrames — how often to log.
     */
    VROMetalFrameTimer(id <MTLDevice> device, int windowFrames = 300, int reportEveryFrames = 300);
    ~VROMetalFrameTimer();

    /*
     True when per-pass GPU timing is available. When false, only CPU and total GPU time are
     reported, and the per-pass table is omitted rather than filled with zeroes.
     */
    bool supportsPassTiming() const { return _sampleBuffer != nil; }

    // ── Frame lifecycle ──────────────────────────────────────────────────────

    void beginFrame(id <MTLCommandBuffer> commandBuffer);

    /*
     Attach timestamp sampling for one render pass. Returns false if there is no room left in
     this frame's sample buffer or per-pass timing is unavailable, in which case the pass is
     simply not measured — never a reason to skip rendering it.
     */
    bool beginPass(MTLRenderPassDescriptor *descriptor, const std::string &label);

    /*
     Close out the frame. Reads back the counters and folds this frame into the window;
     reports when the window is full.
     */
    void endFrame();

    // ── Phase timing (CPU) ───────────────────────────────────────────────────

    void beginPhase(const std::string &label);
    void endPhase(const std::string &label);

    /*
     Log a report now rather than waiting for the window to fill. Useful at teardown.
     */
    void report();

private:

    struct Stats {
        std::vector<double> samples;   // milliseconds
        void add(double ms) { samples.push_back(ms); }
        bool empty() const { return samples.empty(); }
        double min() const;
        double median() const;
        double p95() const;
        double max() const;
    };

    struct PendingPass {
        std::string label;
        int startIndex;
        int endIndex;
    };

    /*
     One frame's measured passes, shared with that frame's completion handler.
     The handler has to be installed before the command buffer is committed, which is before
     any pass has run — so it captures this record and reads it once the GPU is done.
     */
    struct FrameRecord {
        std::vector<PendingPass> passes;
    };

    id <MTLDevice> _device;
    id <MTLCounterSampleBuffer> _sampleBuffer;
    id <MTLCommandBuffer> _commandBuffer;

    int _windowFrames;
    int _reportEveryFrames;
    int _frameCount;
    int _sampleCursor;
    int _sampleCapacity;

    std::shared_ptr<FrameRecord> _currentFrame;
    std::map<std::string, Stats> _passStats;
    std::map<std::string, Stats> _phaseStats;
    std::map<std::string, double> _phaseStarts;   // seconds
    Stats _gpuTotal;
    Stats _cpuFrame;
    double _frameStart;

    // GPU timestamps are in an opaque tick domain; this converts them to milliseconds.
    double _gpuTicksToMs;
    void calibrateTimestampDomain();
};

#endif  // VRO_METAL
#endif /* VROMetalFrameTimer_h */
