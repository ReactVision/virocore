//
//  VROImageWasm.h
//  ViroRenderer
//
//  Created by Raj Advani on 3/8/18.
//  Copyright © 2016 Viro Media. All rights reserved.
//

#ifndef VROImageWasm_h
#define VROImageWasm_h

#include "VROImage.h"
#include "SDL_image.h"
#include <string>

class VROImageWasm : public VROImage {
    
public:
    
    /*
     Construct a new VROImage from the given file that's been preloaded
     or downloaded.
     */
    VROImageWasm(std::string file, VROTextureInternalFormat format);
    /*
     Construct a new VROImage from an in-memory encoded image buffer
     (e.g. a PNG/JPEG embedded in a GLB/VRX model). Format is auto-detected.
     */
    VROImageWasm(void *data, int length, VROTextureInternalFormat format);
    virtual ~VROImageWasm();
    
    int getWidth() const;
    int getHeight() const;
    unsigned char *getData(size_t *length);
    
    void lock();
    void unlock();
    
private:

    void initFromData(void *data, int length, const char *ext);
    SDL_Surface *_surface;
    SDL_Surface *convertToRGBA8(SDL_Surface *surface);
    
};

#endif /* VROImageWasm_h */
