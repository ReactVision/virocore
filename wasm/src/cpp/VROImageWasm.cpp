//
//  VROImageWasm.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 3/8/18.
//  Copyright © 2018 Viro Media. All rights reserved.
//

#include "VROImageWasm.h"
#include "VROLog.h"
#include "VROTime.h"
#include "VROJpegReader.h"
#include "VROPlatformUtil.h"

VROImageWasm::VROImageWasm(std::string file, VROTextureInternalFormat internalFormat) {
    int length;
    void *data = VROPlatformLoadFile(file, &length);

    char *ext = (char *)strrchr(file.c_str(), '.');
    if (ext) {
        ext++;
    }

    initFromData(data, length, ext);
    free (data);

    if (_surface == NULL) {
        pinfo("Failed to load image at path [%s], error [%s]", file.c_str(), SDL_GetError());
    }
}

VROImageWasm::VROImageWasm(void *data, int length, VROTextureInternalFormat internalFormat) {
    // In-memory encoded image (e.g. embedded in a GLB/VRX). Format auto-detected.
    initFromData(data, length, NULL);
    if (_surface == NULL) {
        pinfo("Failed to load image from buffer, error [%s]", SDL_GetError());
    }
}

void VROImageWasm::initFromData(void *data, int length, const char *ext) {
    _surface = NULL;

    // Detect JPEG by magic bytes (FF D8 FF) and decode with libjpeg via
    // VROJpegReader — the emscripten SDL_image port is built PNG-only, so JPEGs
    // (common in GLB/VRX textures) must go through our own decoder.
    const unsigned char *bytes = (const unsigned char *) data;
    bool isJpeg = (length >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF);

    if (isJpeg) {
        _surface = VROJpegReader::loadJPG(data, length);
    } else {
        SDL_RWops *src = SDL_RWFromMem(data, length);
        // With an extension hint use the typed loader; otherwise auto-detect.
        _surface = ext ? IMG_LoadTyped_RW(src, 1, (char *) ext) : IMG_Load_RW(src, 1);
    }

    if (_surface == NULL) {
        return;
    }

    // We always convert to RGBA8 because sRGB8 is not compatible with automatic mipmap
    // generation in OpenGL 3.0).
    int bytesPerPixel = _surface->format->BytesPerPixel;
    if (bytesPerPixel <= 3) {
        SDL_Surface *rgbaSurface = convertToRGBA8(_surface);
        SDL_FreeSurface(_surface);
        _surface = rgbaSurface;

        if (_surface == NULL) {
            pinfo("Failed to convert surface to RGBA8 [%s]", SDL_GetError());
        }
    }

    _format = bytesPerPixel <= 3 ? VROTextureFormat::RGB8 : VROTextureFormat::RGBA8;
    _internalFormat = VROTextureInternalFormat::RGBA8;
}

VROImageWasm::~VROImageWasm() {
    if (_surface != NULL) {
        SDL_FreeSurface(_surface);
    }
}

int VROImageWasm::getHeight() const {
    if (_surface == NULL) { return 0; }
    return _surface->h;
}

int VROImageWasm::getWidth() const {
    if (_surface == NULL) { return 0; }
    return _surface->w;
}

void VROImageWasm::lock() {
    if (_surface == NULL) { return; }
    SDL_LockSurface(_surface);
}

void VROImageWasm::unlock() {
    if (_surface == NULL) { return; }
    SDL_UnlockSurface(_surface);
}

unsigned char *VROImageWasm::getData(size_t *length) {
    if (_surface == NULL) { return NULL; }
    
    if (_format == VROTextureFormat::RGB8) {
        *length = getHeight() * getWidth() * 3;
    } else {
        *length = getHeight() * getWidth() * 4;
    }
    return (unsigned char *)_surface->pixels;
}

SDL_Surface *VROImageWasm::convertToRGBA8(SDL_Surface *image) {
    SDL_PixelFormat fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.BitsPerPixel = 32;
    fmt.BytesPerPixel = 4;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    fmt.Rmask = 0xff000000;
    fmt.Gmask = 0x00ff0000;
    fmt.Bmask = 0x0000ff00;
    fmt.Amask = 0x000000ff;
#else
    fmt.Rmask = 0x000000ff;
    fmt.Gmask = 0x0000ff00;
    fmt.Bmask = 0x00ff0000;
    fmt.Amask = 0xff000000;
#endif
    return SDL_ConvertSurface(image, &fmt, SDL_SWSURFACE);
}
