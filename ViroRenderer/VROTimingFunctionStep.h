//
//  VROTimingFunctionStep.h
//  ViroRenderer
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

#ifndef VROTimingFunctionStep_h
#define VROTimingFunctionStep_h

#include "VROTimingFunction.h"

/*
 Step interpolation holds the previous keyframe value until the next keyframe
 is reached, then instantly jumps to that value. This implements the glTF 2.0
 STEP interpolation mode.

 The timing function returns 0.0 for all t < 1.0, and 1.0 when t == 1.0.
 This causes the animation system to hold the start value until the end
 of each keyframe interval, then jump to the end value.
 */
class VROTimingFunctionStep : public VROTimingFunction {
public:

    VROTimingFunctionStep() {}
    virtual ~VROTimingFunctionStep() {}

    float getT(float t) {
        // Hold at 0 until we reach the end of the interval
        return (t >= 1.0f) ? 1.0f : 0.0f;
    }
};

#endif /* VROTimingFunctionStep_h */
