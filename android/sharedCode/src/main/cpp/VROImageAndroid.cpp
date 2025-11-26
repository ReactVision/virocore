//
//  VROImageAndroid.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/10/16.
//  Copyright © 2016 Viro Media. All rights reserved.
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

#include "VROImageAndroid.h"
#include "VROPlatformUtil.h"
#include <stdlib.h>

VROImageAndroid::VROImageAndroid(std::string asset, VROTextureInternalFormat internalFormat) :
    _grayscaleData(nullptr) {

    jobject jbitmap = VROPlatformLoadBitmapFromAsset(asset, internalFormat);
    bool hasAlpha;
    _data = (unsigned char *)VROPlatformConvertBitmap(jbitmap, &_dataLength, &_width, &_height, &hasAlpha);

    if (internalFormat == VROTextureInternalFormat::RGB565) {
        _format = VROTextureFormat::RGB565;
        _internalFormat = VROTextureInternalFormat::RGB565;
    } else {
        // Note that VROPlatformLoadBitmapFromAsset always generates RGBA8, even from
        // RGB8 images. This is considered optimal because sRGB8 is not compatible with
        // automatic mipmap generation in OpenGL 3.0
        _format = hasAlpha ? VROTextureFormat::RGBA8 : VROTextureFormat::RGB8;
        _internalFormat = VROTextureInternalFormat::RGBA8;
    }
}

VROImageAndroid::VROImageAndroid(jobject jbitmap, VROTextureInternalFormat internalFormat) :
    _grayscaleData(nullptr) {

    bool hasAlpha = false;
    _data = (unsigned char *)VROPlatformConvertBitmap(jbitmap, &_dataLength, &_width, &_height, &hasAlpha);
    if (internalFormat == VROTextureInternalFormat::RGB565) {
        _format = VROTextureFormat::RGB565;
        _internalFormat = VROTextureInternalFormat::RGB565;
    } else {
        // Internal format is always RGBA8, even for images that do not have alpha. This is because
        // sRGB8 is not compatible with automatic mipmap generation in OpenGL 3.0 (so we use sRGBA8).
        _format = hasAlpha ? VROTextureFormat::RGBA8 : VROTextureFormat::RGB8;
        _internalFormat = VROTextureInternalFormat::RGBA8;
    }
}

VROImageAndroid::VROImageAndroid(jobject jbitmap) :
    _grayscaleData(nullptr) {

    _format = VROPlatformGetBitmapFormat(jbitmap);
    bool hasAlpha;
    _data = (unsigned char *)VROPlatformConvertBitmap(jbitmap, &_dataLength, &_width, &_height, &hasAlpha);
    _grayscaleData = nullptr;
}

VROImageAndroid::~VROImageAndroid() {
    free(_data);
    if (_grayscaleData != nullptr) {
        free(_grayscaleData);
    }
}

int VROImageAndroid::getWidth() const {
    return _width;
}

int VROImageAndroid::getHeight() const {
    return _height;
}

unsigned char *VROImageAndroid::getData(size_t *length) {
    *length = _dataLength;
    return _data;
}

/*
 This function is used by VROARImageTargetAndroid (w/ ARCore). As such, we can
 make a few assumptions, that the data is from a Bitmap and is using the RGBA_8888
 format.

 IMPORTANT: ARCore requires stride == width for augmented images. We always
 return a tightly-packed grayscale buffer.
 */
unsigned char *VROImageAndroid::getGrayscaleData(size_t *length, size_t *stride) {
    *length = _width * _height; // tightly-packed grayscale (no padding)
    *stride = _width; // ARCore requires stride == width

    if (_grayscaleData == nullptr) {
        // Calculate the RGBA stride from the original data
        int32_t rgbastride = _dataLength / _height;
        convertRgbaToGrayscale(rgbastride, &_grayscaleData);
    }
    return _grayscaleData;
}

/*
 This function comes from the augmented_image_c example provided by ARCore (util.h's
 ConvertRgbaToGrayscale function).

 IMPORTANT: ARCore requires stride == width for augmented images. We always
 output a tightly-packed grayscale buffer (no row padding) regardless of the
 input RGBA stride.
 */
void VROImageAndroid::convertRgbaToGrayscale(int32_t stride, uint8_t **out_grayscale_buffer) {
    // Always use width as the output stride (no padding) - ARCore requires stride == width
    uint8_t *grayscale_buffer = new uint8_t[_width * _height];
    for (int h = 0; h < _height; ++h) {
        for (int w = 0; w < _width; ++w) {
            const uint8_t *pixel = &_data[w * 4 + h * stride];
            uint8_t r = *pixel;
            uint8_t g = *(pixel + 1);
            uint8_t b = *(pixel + 2);
            // Write to tightly-packed buffer (stride == width)
            grayscale_buffer[w + h * _width] =
                    static_cast<uint8_t>(0.213f * r + 0.715 * g + 0.072 * b);
        }
    }
    *out_grayscale_buffer = grayscale_buffer;
}
