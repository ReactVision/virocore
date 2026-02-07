//
//  VRODriverOpenGL.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/19/18.
//  Copyright © 2018 Viro Media. All rights reserved.
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
#include "VRODriverOpenGL.h"

// Note this class has limited functionality (most is in the header) but we still require a
// cpp file in order to have a 'key function' which guarantees we get a strong global symbol
// for this class in the typeinfo of the library, so that dynamic_cast works across dlopen
// boundaries.
//
// See here: https://github.com/android-ndk/ndk/issues/533#issuecomment-335977747
VRODriverOpenGL::VRODriverOpenGL() :
        _gpuType(VROGPUType::Normal),
        _lastPurgeFrame(0),
        _softwareGammaPass(false),
        _depthWritingEnabled(true),
        _depthReadingEnabled(true),
        _materialColorWritingMask(VROColorMaskAll),
        _renderTargetColorWritingMask(VROColorMaskAll),
        _aggregateColorWritingMask(VROColorMaskAll),
        _cullMode(VROCullMode::None),
        _blendMode(VROBlendMode::Alpha) {

    _shaderFactory = std::unique_ptr<VROShaderFactory>(new VROShaderFactory());
    _scheduler = std::make_shared<VROFrameScheduler>();
}

VRODriverOpenGL::~VRODriverOpenGL() {
    // Delete all moribund (pending deletion) GPU resources
    std::lock_guard<std::recursive_mutex> lock(_deletionMutex);

    if (!_moribundTextures.empty()) {
        GL( glDeleteTextures(_moribundTextures.size(), &_moribundTextures[0]) );
        _moribundTextures.clear();
    }

    if (!_moribundFramebuffers.empty()) {
        GL( glDeleteFramebuffers(_moribundFramebuffers.size(), &_moribundFramebuffers[0]) );
        _moribundFramebuffers.clear();
    }

    if (!_moribundRenderbuffers.empty()) {
        GL( glDeleteRenderbuffers(_moribundRenderbuffers.size(), &_moribundRenderbuffers[0]) );
        _moribundRenderbuffers.clear();
    }

    if (!_moribundBuffers.empty()) {
        GL( glDeleteBuffers(_moribundBuffers.size(), &_moribundBuffers[0]) );
        _moribundBuffers.clear();
    }

    if (!_moribundShaders.empty()) {
        for (auto shader : _moribundShaders) {
            GL( glDeleteShader(shader) );
        }
        _moribundShaders.clear();
    }

    if (!_moribundPrograms.empty()) {
        for (auto program : _moribundPrograms) {
            GL( glDeleteProgram(program) );
        }
        _moribundPrograms.clear();
    }

    // Clear shader factory to release compiled shaders
    _shaderFactory.reset();

    // Clear lighting UBOs
    _lightingUBOs.clear();
}
