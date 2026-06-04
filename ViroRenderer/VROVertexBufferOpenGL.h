//
//  VROVertexBufferOpenGL.h
//  ViroKit
//
//  Created by Raj Advani on 6/29/19.
//  Copyright © 2019 Viro Media. All rights reserved.
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

#ifndef VROVertexBufferOpenGL_h
#define VROVertexBufferOpenGL_h

#include <stdio.h>
#include <memory>
#include "VROOpenGL.h"
#include "VROVertexBuffer.h"

class VRODriverOpenGL;

class VROVertexBufferOpenGL : public VROVertexBuffer {
public:

    VROVertexBufferOpenGL(std::shared_ptr<VROData> data,
                          std::shared_ptr<VRODriverOpenGL> driver);
    VROVertexBufferOpenGL(std::shared_ptr<VROData> data,
                          VROVertexBufferUsage usage,
                          std::shared_ptr<VRODriverOpenGL> driver);
    virtual ~VROVertexBufferOpenGL();

    virtual void hydrate();
    /*
     Replace the CPU data and re-upload to the existing VBO via glBufferSubData.
     For Dynamic / Stream usage, the buffer is orphaned first (glBufferData with
     null data) to avoid GPU stalls on driver implementations that wait for the
     previous draw to finish. Static buffers warn and ignore the update.
     */
    virtual void updateData(std::shared_ptr<VROData> newData);

    GLuint getVBO() const { return _buffer; }

private:

    GLuint _buffer;
    GLsizeiptr _bufferCapacity;     // bytes allocated on the GPU at hydrate() time
    std::weak_ptr<VRODriverOpenGL> _driver;

};

#endif /* VROVertexBufferOpenGL_h */
