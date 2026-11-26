//
//  VROInputPresenterWasm.h
//  ViroRenderer
//
//  Copyright © 2018 Viro Media. All rights reserved.
//

#ifndef VROInputPresenterWasm_H
#define VROInputPresenterWasm_H

#include <memory>
#include <string>
#include <vector>
#include "VRODefines.h"
#include "VROEventDelegate.h"

// No reticle: the web points with a mouse or a finger, so a gaze cursor fixed
// at the centre of the view only covers the scene.
class VROInputPresenterWasm : public VROInputPresenter {
public:
    VROInputPresenterWasm() {}
    ~VROInputPresenterWasm() {}

    void onClick(int source, std::shared_ptr<VRONode> node, ClickState clickState, std::vector<float> position) {
        VROInputPresenter::onClick(source, node, clickState, position);
    }

    void onGazeHit(int source, std::shared_ptr<VRONode> node, const VROHitTestResult &hit) {
        VROInputPresenter::onReticleGazeHit(hit);
     }
};
#endif
