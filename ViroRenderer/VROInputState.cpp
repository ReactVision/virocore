//
//  VROInputState.cpp
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
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

#include "VROInputState.h"

VROInputState::Snapshot VROInputState::snapshot() const {
    std::lock_guard<std::mutex> lock(_mtx);
    return _state;
}

void VROInputState::setStickL(float x, float y) {
    std::lock_guard<std::mutex> lock(_mtx);
    _state.stickLX = x;
    _state.stickLY = y;
}

void VROInputState::setStickR(float x, float y) {
    std::lock_guard<std::mutex> lock(_mtx);
    _state.stickRX = x;
    _state.stickRY = y;
}

void VROInputState::setTriggerL(float value) {
    std::lock_guard<std::mutex> lock(_mtx);
    _state.triggerL = value;
}

void VROInputState::setTriggerR(float value) {
    std::lock_guard<std::mutex> lock(_mtx);
    _state.triggerR = value;
}

void VROInputState::setButton(int idx, bool pressed) {
    if (idx < 0 || idx >= kMaxButtons) {
        return;
    }
    std::lock_guard<std::mutex> lock(_mtx);
    uint32_t bit = 1u << idx;
    if (pressed) {
        _state.buttonBits |= bit;
    } else {
        _state.buttonBits &= ~bit;
    }
}

void VROInputState::reset() {
    std::lock_guard<std::mutex> lock(_mtx);
    _state = Snapshot{};
}
