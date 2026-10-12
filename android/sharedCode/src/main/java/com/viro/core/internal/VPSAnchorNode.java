//
//  Copyright © 2026 ReactVision.
//  All rights reserved.
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

package com.viro.core.internal;

import com.viro.core.Node;

import java.lang.ref.WeakReference;

/**
 * @hide
 * A Node whose children are in the VPS map's frame. Registered with an ARScene
 * ({@link com.viro.core.ARScene#addVPSAnchorNode}), it follows the map's transform every
 * frame and stays hidden until VPS localisation has converged. See VROVPSAnchorNode.
 */
public class VPSAnchorNode extends Node {

    public interface Listener {
        void onPlacedChanged(boolean placed);
    }

    private WeakReference<Listener> mListener = null;

    public VPSAnchorNode() {
        super(false);
        initWithNativeRef(nativeCreateVPSAnchorNode());
        nativeSetPlacedListener(mNativeRef);
    }

    public void setListener(Listener listener) {
        mListener = new WeakReference<Listener>(listener);
    }

    /* Invoked by native, on the application thread. */
    void onPlacedChanged(boolean placed) {
        Listener listener;
        if (mListener != null && (listener = mListener.get()) != null) {
            listener.onPlacedChanged(placed);
        }
    }

    private native long nativeCreateVPSAnchorNode();
    private native void nativeSetPlacedListener(long nativeRef);
}
