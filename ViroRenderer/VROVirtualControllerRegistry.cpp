//
//  VROVirtualControllerRegistry.cpp
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

#include "VROVirtualControllerRegistry.h"
#include "VROInputState.h"

VROVirtualControllerRegistry &VROVirtualControllerRegistry::instance() {
    // Meyers' singleton — thread-safe initialisation since C++11.
    static VROVirtualControllerRegistry inst;
    return inst;
}

std::shared_ptr<VROInputState> VROVirtualControllerRegistry::acquire(const std::string &id) {
    std::lock_guard<std::mutex> lock(_mtx);
    Entry &e = _entries[id];
    if (!e.state) {
        e.state = std::make_shared<VROInputState>();
    }
    e.refCount += 1;
    return e.state;
}

std::shared_ptr<VROInputState> VROVirtualControllerRegistry::peek(const std::string &id) const {
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _entries.find(id);
    if (it == _entries.end()) {
        return nullptr;
    }
    return it->second.state;
}

void VROVirtualControllerRegistry::release(const std::string &id) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _entries.find(id);
    if (it == _entries.end()) {
        return;
    }
    it->second.refCount -= 1;
    if (it->second.refCount <= 0) {
        _entries.erase(it);
    }
}

size_t VROVirtualControllerRegistry::size() const {
    std::lock_guard<std::mutex> lock(_mtx);
    return _entries.size();
}
