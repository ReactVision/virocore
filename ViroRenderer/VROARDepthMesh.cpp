//
//  VROARDepthMesh.cpp
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

#include "VROARDepthMesh.h"
#include <numeric>

VROARDepthMesh::VROARDepthMesh() {}

VROARDepthMesh::VROARDepthMesh(std::vector<VROVector3f> vertices,
                               std::vector<int> indices,
                               std::vector<float> confidences)
    : _vertices(std::move(vertices)),
      _indices(std::move(indices)),
      _confidences(std::move(confidences)) {
}

VROARDepthMesh::~VROARDepthMesh() {}

float VROARDepthMesh::getAverageConfidence() const {
    if (_confidences.empty()) {
        return 0.0f;
    }
    float sum = std::accumulate(_confidences.begin(), _confidences.end(), 0.0f);
    return sum / static_cast<float>(_confidences.size());
}

bool VROARDepthMesh::isValid() const {
    return !_vertices.empty() &&
           !_indices.empty() &&
           _indices.size() % 3 == 0;
}
