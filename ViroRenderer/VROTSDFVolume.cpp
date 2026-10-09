//
//  VROTSDFVolume.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VROTSDFVolume.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

    /* Floor division, so a negative coordinate lands in the block below rather than rounding
       toward zero and folding the two sides of the origin onto each other. */
    inline int floorDiv(int a, int b) {
        int q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }

    inline int floorMod(int a, int b) {
        int r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) r += b;
        return r;
    }

    inline int floorToInt(float v) {
        return (int)std::floor(v);
    }

}  // namespace

VROTSDFVolume::VROTSDFVolume(float voxelSize, float truncation, size_t maxBlocks)
    : _voxelSize(voxelSize > 0.0f ? voxelSize : 0.04f),
      _truncation(truncation > 0.0f ? truncation : 0.12f),
      _maxBlocks(maxBlocks > 0 ? maxBlocks : 16384) {
}

uint64_t VROTSDFVolume::blockKey(int bx, int by, int bz) {
    const uint64_t mask = (1ull << 21) - 1;
    return ((uint64_t)(bx + (1 << 20)) & mask) << 42
         | ((uint64_t)(by + (1 << 20)) & mask) << 21
         | ((uint64_t)(bz + (1 << 20)) & mask);
}

void VROTSDFVolume::unpackCell(uint64_t key, int *gx, int *gy, int *gz) {
    const uint64_t mask = (1ull << 21) - 1;
    *gz = (int)((key        ) & mask) - (1 << 20);
    *gy = (int)((key >> 21  ) & mask) - (1 << 20);
    *gx = (int)((key >> 42  ) & mask) - (1 << 20);
}

VROTSDFVolume::Voxel *VROTSDFVolume::voxelAt(int gx, int gy, int gz, bool create) {
    const int bx = floorDiv(gx, kBlockSize);
    const int by = floorDiv(gy, kBlockSize);
    const int bz = floorDiv(gz, kBlockSize);
    const uint64_t key = blockKey(bx, by, bz);

    auto it = _blocks.find(key);
    if (it == _blocks.end()) {
        if (!create) return nullptr;
        auto inserted = _blocks.emplace(key, std::unique_ptr<Block>(new Block()));
        it = inserted.first;
    }
    it->second->lastTouched = _frameCounter;

    const int lx = floorMod(gx, kBlockSize);
    const int ly = floorMod(gy, kBlockSize);
    const int lz = floorMod(gz, kBlockSize);
    return &it->second->voxels[(lz * kBlockSize + ly) * kBlockSize + lx];
}

const VROTSDFVolume::Voxel *VROTSDFVolume::voxelAt(int gx, int gy, int gz) const {
    const int bx = floorDiv(gx, kBlockSize);
    const int by = floorDiv(gy, kBlockSize);
    const int bz = floorDiv(gz, kBlockSize);

    auto it = _blocks.find(blockKey(bx, by, bz));
    if (it == _blocks.end()) return nullptr;

    const int lx = floorMod(gx, kBlockSize);
    const int ly = floorMod(gy, kBlockSize);
    const int lz = floorMod(gz, kBlockSize);
    return &it->second->voxels[(lz * kBlockSize + ly) * kBlockSize + lx];
}

void VROTSDFVolume::integrate(const std::vector<VROVector3f> &points,
                              const std::vector<float> &confidences,
                              const VROVector3f &cameraPosition) {
    if (points.empty()) return;
    ++_frameCounter;

    // How far around a sample to touch: the truncation, in voxels.
    const int reach = std::max(1, (int)std::ceil(_truncation / _voxelSize));

    for (size_t i = 0; i < points.size(); ++i) {
        const VROVector3f &p = points[i];
        const float w = (i < confidences.size()) ? std::max(0.0f, confidences[i]) : 1.0f;
        if (w <= 0.0f) continue;

        // Direction from the camera to the sample. Distances along it are what carries the sign:
        // the surface is at `dist`, so a voxel nearer the camera is in front of it and a voxel
        // past it is behind.
        VROVector3f ray = p.subtract(cameraPosition);
        const float dist = ray.magnitude();
        if (dist <= 1e-4f) continue;
        ray = ray.scale(1.0f / dist);

        const int cx = floorToInt(p.x / _voxelSize);
        const int cy = floorToInt(p.y / _voxelSize);
        const int cz = floorToInt(p.z / _voxelSize);

        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dy = -reach; dy <= reach; ++dy) {
                for (int dx = -reach; dx <= reach; ++dx) {
                    const int gx = cx + dx, gy = cy + dy, gz = cz + dz;

                    // Voxel centre, in world space.
                    VROVector3f centre(((float)gx + 0.5f) * _voxelSize,
                                       ((float)gy + 0.5f) * _voxelSize,
                                       ((float)gz + 0.5f) * _voxelSize);

                    // Distance from the camera to the voxel, measured along the sample's ray.
                    const float t = centre.subtract(cameraPosition).dot(ray);
                    const float sdf = dist - t;

                    // Behind the surface by more than the truncation: that space is occluded, and
                    // nothing was observed about it.
                    if (sdf < -_truncation) continue;
                    // In front by more than the truncation: free space, which this volume does not
                    // model, so leave it alone rather than carving.
                    if (sdf > _truncation) continue;

                    // Off the ray: the sample says nothing about voxels to the side of it.
                    const VROVector3f onRay = cameraPosition.add(ray.scale(t));
                    if (centre.subtract(onRay).magnitude() > _voxelSize) continue;

                    Voxel *v = voxelAt(gx, gy, gz, true);
                    if (!v) continue;
                    _dirtyBlocks.insert(blockKey(floorDiv(gx, kBlockSize),
                                                 floorDiv(gy, kBlockSize),
                                                 floorDiv(gz, kBlockSize)));

                    const float newWeight = v->weight + w;
                    v->sdf = (v->sdf * v->weight + sdf * w) / newWeight;
                    // Cap the weight so a surface that moves can still be corrected rather than
                    // being pinned by thousands of old observations.
                    v->weight = std::min(newWeight, 64.0f);
                    _dirty = true;
                }
            }
        }
    }

    evictIfNeeded();
}

void VROTSDFVolume::evictIfNeeded() {
    if (_blocks.size() <= _maxBlocks) return;

    // Oldest first, by the frame they were last written in.
    std::vector<std::pair<uint64_t, uint64_t>> byAge;  // (lastTouched, key)
    byAge.reserve(_blocks.size());
    for (const auto &entry : _blocks) {
        byAge.emplace_back(entry.second->lastTouched, entry.first);
    }
    const size_t excess = _blocks.size() - _maxBlocks;
    std::partial_sort(byAge.begin(), byAge.begin() + excess, byAge.end());
    for (size_t i = 0; i < excess; ++i) {
        _blocks.erase(byAge[i].second);
        // Its cells no longer have voxels behind them, so they have to be reconsidered rather
        // than left in the cache describing a surface that was dropped.
        _dirtyBlocks.insert(byAge[i].second);
    }
}

void VROTSDFVolume::reset() {
    _blocks.clear();
    _cells.clear();
    _dirtyBlocks.clear();
    _frameCounter = 0;
    _dirty = true;
}

namespace {
    // The eight voxels of a cell, as offsets from its minimum corner.
    const int kCorner[8][3] = {
        {0,0,0}, {1,0,0}, {0,1,0}, {1,1,0},
        {0,0,1}, {1,0,1}, {0,1,1}, {1,1,1}
    };
    // The twelve edges, as pairs of corner indices.
    const int kEdge[12][2] = {
        {0,1},{2,3},{4,5},{6,7},   // along x
        {0,2},{1,3},{4,6},{5,7},   // along y
        {0,4},{1,5},{2,6},{3,7}    // along z
    };
}  // namespace

void VROTSDFVolume::remeshBlock(int bx, int by, int bz) {
    // A cell reads the voxels at its corner and one past it on each axis, so a voxel in this block
    // can be read by a cell that starts in the block before it. Recomputing from one short of the
    // origin covers that overlap; without it a changed block leaves a seam along its low faces.
    const int x0 = bx * kBlockSize - 1, x1 = bx * kBlockSize + kBlockSize;
    const int y0 = by * kBlockSize - 1, y1 = by * kBlockSize + kBlockSize;
    const int z0 = bz * kBlockSize - 1, z1 = bz * kBlockSize + kBlockSize;

    for (int gz = z0; gz < z1; ++gz) {
    for (int gy = y0; gy < y1; ++gy) {
    for (int gx = x0; gx < x1; ++gx) {
        const uint64_t key = cellKey(gx, gy, gz);
        ++_lastRemeshedCells;

        float sdf[8], wgt[8];
        bool complete = true;
        for (int c = 0; c < 8; ++c) {
            const Voxel *v = voxelAt(gx + kCorner[c][0], gy + kCorner[c][1], gz + kCorner[c][2]);
            if (!v || v->weight <= 0.0f) { complete = false; break; }
            sdf[c] = v->sdf;
            wgt[c] = v->weight;
        }
        if (!complete) { _cells.erase(key); continue; }

        bool neg = false, pos = false;
        for (int c = 0; c < 8; ++c) { (sdf[c] < 0.0f) ? neg = true : pos = true; }
        if (!neg || !pos) { _cells.erase(key); continue; }

        // Average of the zero crossings on the edges that change sign. That average is what pulls
        // the vertex onto the surface instead of leaving it at the cell centre.
        VROVector3f sum;
        int crossings = 0;
        float confidence = 0.0f;
        for (int e = 0; e < 12; ++e) {
            const int a = kEdge[e][0], b = kEdge[e][1];
            if ((sdf[a] < 0.0f) == (sdf[b] < 0.0f)) continue;
            const float denom = sdf[a] - sdf[b];
            const float t = (std::fabs(denom) < 1e-9f) ? 0.5f : (sdf[a] / denom);
            // +0.5: an sdf belongs to the voxel's centre, and a centre sits half a voxel past its
            // index. Leaving it out puts the whole surface half a voxel off.
            VROVector3f pa((float)(gx + kCorner[a][0]) + 0.5f,
                           (float)(gy + kCorner[a][1]) + 0.5f,
                           (float)(gz + kCorner[a][2]) + 0.5f);
            VROVector3f pb((float)(gx + kCorner[b][0]) + 0.5f,
                           (float)(gy + kCorner[b][1]) + 0.5f,
                           (float)(gz + kCorner[b][2]) + 0.5f);
            sum = sum.add(pa.add(pb.subtract(pa).scale(t)));
            confidence += 0.5f * (wgt[a] + wgt[b]);
            ++crossings;
        }
        if (crossings == 0) { _cells.erase(key); continue; }

        CellVertex cv;
        cv.position = sum.scale(1.0f / (float)crossings).scale(_voxelSize);
        // Normalised back out of the weight cap, so it reads like the per-sample confidences the
        // non-fused path produces.
        cv.confidence = std::min(1.0f, confidence / (float)crossings / 8.0f);
        _cells[key] = cv;
    }}}
}

void VROTSDFVolume::extractSurface(std::vector<VROVector3f> *outVertices,
                                   std::vector<float> *outConfidences,
                                   std::vector<int> *outIndices) const {
    if (outVertices) outVertices->clear();
    if (outConfidences) outConfidences->clear();
    if (outIndices) outIndices->clear();
    if (!outVertices || !outIndices) return;

    VROTSDFVolume *self = const_cast<VROTSDFVolume *>(this);
    self->_lastRemeshedCells = 0;

    // Only the blocks that moved since the last extraction, not the whole room.
    for (uint64_t key : _dirtyBlocks) {
        const int bz = (int)((key        ) & ((1ull << 21) - 1)) - (1 << 20);
        const int by = (int)((key >> 21  ) & ((1ull << 21) - 1)) - (1 << 20);
        const int bx = (int)((key >> 42  ) & ((1ull << 21) - 1)) - (1 << 20);
        self->remeshBlock(bx, by, bz);
    }
    self->_dirtyBlocks.clear();

    if (_cells.empty()) return;

    // Index every cached cell, then join them across the edges that change sign.
    std::unordered_map<uint64_t, int> index;
    index.reserve(_cells.size() * 2);
    outVertices->reserve(_cells.size());
    for (const auto &entry : _cells) {
        index[entry.first] = (int)outVertices->size();
        outVertices->push_back(entry.second.position);
        if (outConfidences) outConfidences->push_back(entry.second.confidence);
    }

    auto vertexOf = [&](int gx, int gy, int gz) -> int {
        auto it = index.find(cellKey(gx, gy, gz));
        return (it == index.end()) ? -1 : it->second;
    };

    for (const auto &entry : _cells) {
        int gx, gy, gz;
        unpackCell(entry.first, &gx, &gy, &gz);

        const Voxel *v0 = voxelAt(gx, gy, gz);
        if (!v0 || v0->weight <= 0.0f) continue;

        for (int axis = 0; axis < 3; ++axis) {
            const int nx = gx + (axis == 0 ? 1 : 0);
            const int ny = gy + (axis == 1 ? 1 : 0);
            const int nz = gz + (axis == 2 ? 1 : 0);
            const Voxel *v1 = voxelAt(nx, ny, nz);
            if (!v1 || v1->weight <= 0.0f) continue;
            if ((v0->sdf < 0.0f) == (v1->sdf < 0.0f)) continue;

            // The four cells sharing this edge are the ones offset back along the other two axes.
            const int a = (axis + 1) % 3, b = (axis + 2) % 3;
            int off[4][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
            off[1][a] = -1;
            off[2][a] = -1; off[2][b] = -1;
            off[3][b] = -1;

            int q[4];
            bool complete = true;
            for (int i = 0; i < 4; ++i) {
                q[i] = vertexOf(gx + off[i][0], gy + off[i][1], gz + off[i][2]);
                if (q[i] < 0) { complete = false; break; }
            }
            if (!complete) continue;

            // Wind so the normal points away from the surface's inside, which is the side where
            // the sdf is negative.
            const bool flip = (v0->sdf < 0.0f);
            if (flip) {
                outIndices->push_back(q[0]); outIndices->push_back(q[1]); outIndices->push_back(q[2]);
                outIndices->push_back(q[0]); outIndices->push_back(q[2]); outIndices->push_back(q[3]);
            } else {
                outIndices->push_back(q[0]); outIndices->push_back(q[2]); outIndices->push_back(q[1]);
                outIndices->push_back(q[0]); outIndices->push_back(q[3]); outIndices->push_back(q[2]);
            }
        }
    }
}
