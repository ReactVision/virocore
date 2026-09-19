//
//  VROColocationBridge.mm
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#import "VROColocationBridge.h"
#include "VROColocationSession.h"
#include "VROMatrix4f.h"

#include <string>

@implementation VROColocationPeerInfo
@end

namespace {

/** 16 comma-separated floats, column-major — the encoding every layer uses. */
bool parseMatrixCsv(NSString *csv, VROMatrix4f *out) {
    if (csv == nil) return false;
    NSArray<NSString *> *parts = [csv componentsSeparatedByString:@","];
    if (parts.count != 16) return false;

    float v[16];
    for (NSUInteger i = 0; i < 16; i++) {
        v[i] = parts[i].floatValue;
        if (isnan(v[i])) return false;
    }
    *out = VROMatrix4f(v);
    return true;
}

} // namespace

@implementation VROColocationBridge

+ (instancetype)shared {
    static VROColocationBridge *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [[VROColocationBridge alloc] init]; });
    return instance;
}

- (BOOL)isAvailable {
    return VROColocationSession::get().isAvailable() ? YES : NO;
}

- (void)joinRoom:(NSString *)roomId
          apiKey:(NSString *)apiKey
       projectId:(NSString *)projectId
        endpoint:(NSString *)endpoint
      completion:(void (^)(BOOL, NSString *))completion {

    VROColocationSession::get().join(
        std::string(roomId.UTF8String    ?: ""),
        std::string(apiKey.UTF8String    ?: ""),
        std::string(projectId.UTF8String ?: ""),
        std::string(endpoint.UTF8String  ?: ""),
        [completion](bool success, std::string error) {
            if (!completion) return;
            NSString *err = [NSString stringWithUTF8String:error.c_str()] ?: @"";
            // Callbacks arrive on the socket thread; JS-facing work belongs on
            // the main queue, and every caller here is a React Native module.
            dispatch_async(dispatch_get_main_queue(), ^{ completion(success, err); });
        });
}

- (void)leave {
    VROColocationSession::get().leave();
}

- (void)setLocalPoseCsv:(NSString *)csv {
    VROMatrix4f m;
    if (!parseMatrixCsv(csv, &m)) return;
    VROColocationSession::get().setLocalPose(m);
}

- (VROColocationBridgeState)state {
    return (VROColocationBridgeState)VROColocationSession::get().state();
}

- (NSString *)localPeerId {
    return [NSString stringWithUTF8String:
            VROColocationSession::get().localPeerId().c_str()] ?: @"";
}

- (NSArray<VROColocationPeerInfo *> *)peers {
    NSMutableArray<VROColocationPeerInfo *> *out = [NSMutableArray array];
    for (const auto &p : VROColocationSession::get().peers()) {
        VROColocationPeerInfo *info = [[VROColocationPeerInfo alloc] init];
        info.peerId      = [NSString stringWithUTF8String:p.peerId.c_str()] ?: @"";
        info.position    = @[ @(p.position[0]), @(p.position[1]), @(p.position[2]) ];
        info.rotation    = @[ @(p.rotation[0]), @(p.rotation[1]),
                              @(p.rotation[2]), @(p.rotation[3]) ];
        info.timestampMs = p.timestampMs;
        info.localized   = p.localized ? YES : NO;
        [out addObject:info];
    }
    return out;
}

@end
