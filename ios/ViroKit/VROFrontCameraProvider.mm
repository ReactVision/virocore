//
//  VROFrontCameraProvider.mm
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#import "VROFrontCameraProvider.h"
#import "VROARSessioniOS.h"

@implementation VROFrontCameraProvider

+ (void)registerConfigProvider:(ARConfiguration * _Nullable (^)(void))provider {
    // Forward to the process-wide provider consulted by VROARSessioniOS::updateTrackingType.
    // The block type matches VROARSessioniOS::VROARFrontCameraConfigProvider exactly.
    VROARSessioniOS::setFrontCameraConfigProvider(provider);
}

@end
