// Drives the production VROARWorldMesh on the host: updateFromFrame -> fusion -> extraction ->
// stats -> serialization. No reimplementation — the shipping .cpp, linked against minimal
// stand-ins for the platform pieces (dispatch, physics, scene) that fusion never touches.
#include <cstdio>
#include <cmath>
#include <vector>
#include <set>
#include <string>

#include "VROARWorldMesh.h"
#include "VROARFrame.h"
#include "VROARCamera.h"
#include "VROARDepthMesh.h"
#include "VROViewport.h"
#include "VROCameraTexture.h"
#include "VROARHitTestResult.h"

// ── Fake camera ─────────────────────────────────────────────────────────────
class FakeCamera : public VROARCamera {
public:
    VROVector3f position;
    VROARTrackingState tracking = VROARTrackingState::Normal;

    VROARTrackingState getTrackingState() const override { return tracking; }
    VROARTrackingStateReason getLimitedTrackingStateReason() const override { return VROARTrackingStateReason::None; }
    VROMatrix4f getRotation() const override { VROMatrix4f m; m.toIdentity(); return m; }
    VROVector3f getPosition() const override { return position; }
    VROMatrix4f getProjection(VROViewport, float, float, VROFieldOfView *) override { VROMatrix4f m; m.toIdentity(); return m; }
    VROVector3f getImageSize() override { return VROVector3f(640, 480, 0); }
};

// ── Fake frame: hands over a synthetic depth mesh, the way ARCore would ─────
class FakeFrame : public VROARFrame {
public:
    std::shared_ptr<FakeCamera> camera = std::make_shared<FakeCamera>();
    std::shared_ptr<VROARDepthMesh> depthMesh;
    /** ARKit's path: a mesh the platform already accumulated. Takes priority over depthMesh. */
    std::shared_ptr<VROARDepthMesh> anchorMesh;
    std::vector<std::shared_ptr<VROARAnchor>> anchors;

    double getTimestamp() const override { return 0; }
    const std::shared_ptr<VROARCamera> &getCamera() const override { return _cameraBase; }
    VROCameraOrientation getOrientation() const override { return VROCameraOrientation::Portrait; }
    std::vector<std::shared_ptr<VROARHitTestResult>> hitTest(int, int, std::set<VROARHitTestResultType>) override { return {}; }
    std::vector<std::shared_ptr<VROARHitTestResult>> hitTestRay(VROVector3f *, VROVector3f *, std::set<VROARHitTestResultType>) override { return {}; }
    VROMatrix4f getViewportToCameraImageTransform() const override { VROMatrix4f m; m.toIdentity(); return m; }
    float getAmbientLightIntensity() const override { return 1.0f; }
    VROVector3f getAmbientLightColor() const override { return VROVector3f(1,1,1); }
    const std::vector<std::shared_ptr<VROARAnchor>> &getAnchors() const override { return anchors; }
    std::shared_ptr<VROARPointCloud> getPointCloud() override { return nullptr; }

    bool hasDepthData() const override { return depthMesh != nullptr; }
    std::shared_ptr<VROARDepthMesh> generateDepthMesh(int, float, float) override { return depthMesh; }
    std::shared_ptr<VROARDepthMesh> generateMeshAnchorMesh() override { return anchorMesh; }

    void bind() { _cameraBase = camera; }
private:
    std::shared_ptr<VROARCamera> _cameraBase;
};

// A flat wall at `z`, sampled on a grid.
static std::shared_ptr<VROARDepthMesh> wallMesh(float z, float cx, float cy, int n,
                                                const char *source = "depth") {
    std::vector<VROVector3f> v; std::vector<int> idx; std::vector<float> conf;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
        v.push_back(VROVector3f(cx - 0.9f + 1.8f * i / (n - 1), cy - 0.9f + 1.8f * j / (n - 1), z));
        conf.push_back(1.0f);
    }
    for (int i = 0; i + 1 < n; i++) for (int j = 0; j + 1 < n; j++) {
        int a = i*n+j, b=(i+1)*n+j, c=i*n+j+1, d=(i+1)*n+j+1;
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
        idx.push_back(b); idx.push_back(d); idx.push_back(c);
    }
    return std::make_shared<VROARDepthMesh>(std::move(v), std::move(idx), std::move(conf), source);
}

// A 4 x 5 x 2.5 m room — floor, ceiling and four walls, sampled on a 4 cm grid — as the depth
// camera would see it from `eye` looking along `fwd`: only what is in front and within range.
//
// The indices are a plain fan over the kept points. The fusion reads vertices and confidences and
// nothing else (see fuseFrame), so a frame's own triangulation never reaches the volume; they are
// here because a VROARDepthMesh with no indices is not valid.
static std::shared_ptr<VROARDepthMesh> roomPatch(const VROVector3f &eye, const VROVector3f &fwd) {
    static std::vector<VROVector3f> room;
    if (room.empty()) {
        const float W = 4.0f, D = 5.0f, H = 2.5f, step = 0.04f;
        for (float x = -W/2; x <= W/2; x += step) {
            for (float z = -D/2; z <= D/2; z += step) {
                room.push_back(VROVector3f(x, 0.0f, z));        // floor
                room.push_back(VROVector3f(x, H, z));           // ceiling
            }
            for (float y = 0.0f; y <= H; y += step) {
                room.push_back(VROVector3f(x, y, -D/2));
                room.push_back(VROVector3f(x, y,  D/2));
            }
        }
        for (float z = -D/2; z <= D/2; z += step) {
            for (float y = 0.0f; y <= H; y += step) {
                room.push_back(VROVector3f(-W/2, y, z));
                room.push_back(VROVector3f( W/2, y, z));
            }
        }
    }

    std::vector<VROVector3f> v; std::vector<float> conf;
    for (const VROVector3f &p : room) {
        const VROVector3f d = p.subtract(eye);
        const float range = d.magnitude();
        if (range < 0.3f || range > 4.0f) continue;             // the depth camera's useful band
        if (d.dot(fwd) <= 0.35f * range) continue;              // roughly a 70-degree cone
        v.push_back(p);
        conf.push_back(1.0f);
    }

    std::vector<int> idx;
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        idx.push_back((int)i); idx.push_back((int)i + 1); idx.push_back((int)i + 2);
    }
    return std::make_shared<VROARDepthMesh>(std::move(v), std::move(idx), std::move(conf), "depth");
}

int main() {
    int fails = 0;
    auto check = [&](const char *name, bool ok, const char *detail) {
        printf("  %-52s %s%s%s\n", name, ok ? "OK" : "FAIL", detail && *detail ? "  " : "", detail ? detail : "");
        if (!ok) fails++;
    };

    printf("Production VROARWorldMesh pipeline, no physics (null physicsWorld)\n\n");

    auto worldMesh = std::make_shared<VROARWorldMesh>(nullptr);
    worldMesh->setEnabled(true);
    VROWorldMeshConfig cfg;
    cfg.accumulate = true;
    cfg.voxelSize = 0.04f;
    cfg.updateIntervalMs = 0.0;      // no throttling between frames in the test
    cfg.physicsCellSize = 0.0f;      // no physics clustering
    worldMesh->setConfig(cfg);

    // ── 1. Three different views of the same wall: it must accumulate ──────
    FakeFrame f; f.bind();
    float cams[3][2] = {{0.0f,0.0f},{0.6f,0.2f},{-0.5f,-0.3f}};
    for (auto &c : cams) {
        f.camera->position = VROVector3f(c[0], c[1], 0.0f);
        f.depthMesh = wallMesh(-2.0f, c[0]*0.3f, c[1]*0.3f, 40);
        std::unique_ptr<VROARFrame> frame(&f);
        worldMesh->updateFromFrame(frame);
        frame.release();              // owned by the test, not by the unique_ptr
    }

    VROWorldMeshStats stats = worldMesh->getStats();
    check("source is reported as depth", stats.source == VROWorldMeshSource::Depth, "");
    check("the mesh is marked as accumulated", stats.accumulated, "");
    char buf[128];
    snprintf(buf, sizeof(buf), "(%d vertices, %d triangles)", stats.vertexCount, stats.triangleCount);
    check("fusion produced a mesh", stats.vertexCount > 0 && stats.triangleCount > 0, buf);

    // ── 2. The fused geometry sits on the wall ─────────────────────────────
    auto mesh = worldMesh->getCurrentMesh();
    double worst = 0;
    if (mesh) for (const auto &v : mesh->getVertices()) worst = std::max(worst, (double)std::fabs(v.z + 2.0f));
    snprintf(buf, sizeof(buf), "(max deviation %.4f m, voxel 0.040)", worst);
    check("vertices land on the wall", mesh && worst < 0.05, buf);

    // ── 3. Looking at the same thing again does not duplicate ──────────────
    int before = stats.vertexCount;
    f.camera->position = VROVector3f(0,0,0);
    f.depthMesh = wallMesh(-2.0f, 0, 0, 40);
    { std::unique_ptr<VROARFrame> frame(&f); worldMesh->updateFromFrame(frame); frame.release(); }
    int after = worldMesh->getStats().vertexCount;
    snprintf(buf, sizeof(buf), "(%d -> %d)", before, after);
    check("revisiting the same wall does not duplicate vertices", after < before * 1.3, buf);

    // ── 4. The VPS Lite snapshot comes off that mesh ───────────────────────
    VROMatrix4f L; L.toIdentity(); L.translate(0.5f, -0.2f, 1.0f);
    mesh = worldMesh->getCurrentMesh();   // revisiting replaced it; compare against the live one
    std::vector<uint8_t> snap = worldMesh->serializeCurrentMesh(L);
    bool magicOk = snap.size() > 13 && snap[0]=='R' && snap[1]=='V' && snap[2]=='W' && snap[3]=='M';
    snprintf(buf, sizeof(buf), "(%zu bytes)", snap.size());
    check("serializeCurrentMesh produces a valid .rvwm", magicOk, buf);

    auto loaded = VROARWorldMesh::loadMeshSnapshot(snap, L);
    double rt = 0;
    if (loaded && mesh) {
        const auto &a = mesh->getVertices(); const auto &b = loaded->getVertices();
        if (a.size() == b.size()) for (size_t i=0;i<a.size();i++)
            rt = std::max(rt, (double)a[i].subtract(b[i]).magnitude());
        else rt = 1e9;
    } else rt = 1e9;
    snprintf(buf, sizeof(buf), "(max error %.2e m)", rt);
    check("the snapshot comes back identical under the same transform", rt < 1e-4, buf);

    // ── 5. Limited tracking: nothing is integrated ─────────────────────────
    int beforeLimited = worldMesh->getStats().vertexCount;
    f.camera->tracking = VROARTrackingState::Limited;
    f.camera->position = VROVector3f(9, 9, 9);          // absurd pose: integrating it would show
    f.depthMesh = wallMesh(-8.0f, 5, 5, 40);
    { std::unique_ptr<VROARFrame> frame(&f); worldMesh->updateFromFrame(frame); frame.release(); }
    int afterLimited = worldMesh->getStats().vertexCount;
    snprintf(buf, sizeof(buf), "(%d -> %d)", beforeLimited, afterLimited);
    check("limited tracking does not pollute the volume", afterLimited == beforeLimited, buf);
    f.camera->tracking = VROARTrackingState::Normal;

    // ── 6. resetAccumulation empties it ────────────────────────────────────
    worldMesh->resetAccumulation();

    // Before any new frame lands. Emptying the volume is not enough on its own: the mesh fused out
    // of the old one is what getStats() reports and what serializeCurrentMesh() uploads, so a
    // snapshot taken in this window used to attach the room the app had just cleared.
    VROWorldMeshStats cleared = worldMesh->getStats();
    snprintf(buf, sizeof(buf), "(%d vertices, %d triangles)", cleared.vertexCount, cleared.triangleCount);
    check("a reset zeroes the stats straight away", cleared.vertexCount == 0 && cleared.triangleCount == 0, buf);

    std::vector<uint8_t> emptySnap = worldMesh->serializeCurrentMesh(L);
    snprintf(buf, sizeof(buf), "(%zu bytes)", emptySnap.size());
    check("and leaves nothing for VPS Lite to upload", emptySnap.empty(), buf);

    f.camera->position = VROVector3f(0,0,0);
    f.depthMesh = wallMesh(-2.0f, 0, 0, 40);
    { std::unique_ptr<VROARFrame> frame(&f); worldMesh->updateFromFrame(frame); frame.release(); }
    int afterReset = worldMesh->getStats().vertexCount;
    snprintf(buf, sizeof(buf), "(%d after reset, %d before)", afterReset, beforeLimited);
    check("resetAccumulation starts from empty", afterReset > 0 && afterReset <= beforeLimited, buf);

    // ── 7. accumulate=false reproduces the per-frame behaviour ─────────────
    auto perFrame = std::make_shared<VROARWorldMesh>(nullptr);
    perFrame->setEnabled(true);
    VROWorldMeshConfig c2 = cfg; c2.accumulate = false;
    perFrame->setConfig(c2);
    for (auto &c : cams) {
        f.camera->position = VROVector3f(c[0], c[1], 0.0f);
        f.depthMesh = wallMesh(-2.0f, c[0]*0.3f, c[1]*0.3f, 40);
        std::unique_ptr<VROARFrame> frame(&f); perFrame->updateFromFrame(frame); frame.release();
    }
    VROWorldMeshStats s2 = perFrame->getStats();
    snprintf(buf, sizeof(buf), "(%d vertices = the frame's own 1600)", s2.vertexCount);
    check("accumulate=false hands back the frame mesh", !s2.accumulated && s2.vertexCount == 1600, buf);

    // ── 7b. iOS: ARKit's mesh anchors ─────────────────────────────────────
    // The fusion deliberately leaves this path alone, because ARKit already accumulates. What it
    // must not do is report the result as single-frame: an app on an iPhone with LiDAR reads the
    // same `accumulated` field, and a false there says its working mesh covers only the view.
    {
        auto lidar = std::make_shared<VROARWorldMesh>(nullptr);
        lidar->setEnabled(true);
        lidar->setConfig(cfg);
        f.camera->position = VROVector3f(0, 0, 0);
        f.depthMesh = nullptr;
        f.anchorMesh = wallMesh(-2.0f, 0, 0, 40, "lidar");
        { std::unique_ptr<VROARFrame> frame(&f); lidar->updateFromFrame(frame); frame.release(); }

        VROWorldMeshStats s3 = lidar->getStats();
        snprintf(buf, sizeof(buf), "(source=%s, accumulated=%d)",
                 VROWorldMeshSourceToString(s3.source), (int)s3.accumulated);
        check("mesh anchors report lidar, and report it as accumulated",
              s3.source == VROWorldMeshSource::LiDAR && s3.accumulated, buf);

        // And the snapshot VPS Lite uploads has to come off that path too, not just the fused one.
        std::vector<uint8_t> lidarSnap = lidar->serializeCurrentMesh(L);
        snprintf(buf, sizeof(buf), "(%zu bytes)", lidarSnap.size());
        check("and still serialize for VPS Lite", lidarSnap.size() > 13, buf);

        f.anchorMesh = nullptr;
    }

    // ── 8. Re-meshing is incremental ──────────────────────────────────────
    // Extracting a room costs tens of milliseconds, and a walk changes a handful of blocks per
    // frame. A second extraction with nothing new must do no meshing at all; if it does, dirty
    // tracking is broken and the cost grows with the room rather than with what moved.
    {
        VROTSDFVolume vol(0.04f, 0.12f, 16384);
        auto wall = wallMesh(-2.0f, 0, 0, 40);
        vol.integrate(wall->getVertices(), wall->getConfidences(), VROVector3f(0, 0, 0));

        std::vector<VROVector3f> v1, v2;
        std::vector<float> c1, c2;
        std::vector<int> i1, i2;
        vol.extractSurface(&v1, &c1, &i1);
        const size_t firstPass = vol.getLastRemeshedCellCount();
        vol.extractSurface(&v2, &c2, &i2);
        const size_t secondPass = vol.getLastRemeshedCellCount();

        snprintf(buf, sizeof(buf), "(%zu cells, then %zu)", firstPass, secondPass);
        check("an unchanged volume re-meshes nothing", firstPass > 0 && secondPass == 0, buf);

        snprintf(buf, sizeof(buf), "(%zu/%zu vertices, %zu/%zu triangles)",
                 v1.size(), v2.size(), i1.size() / 3, i2.size() / 3);
        check("and still returns the same surface",
              v1.size() == v2.size() && i1.size() == i2.size(), buf);
    }

    // ── 9. A walk around a room ───────────────────────────────────────────
    // The acceptance criteria are written against a 4 x 5 m room walked for 60 seconds: the vertex
    // count must climb rather than rise and fall with where the camera points, the walk must build
    // at least 20,000 vertices, and the volume must stay inside its memory budget. None of those
    // three need a depth sensor to answer — they are properties of the fusion, and a synthetic room
    // answers them here instead of leaving all six criteria waiting on hardware.
    //
    // What this still cannot say anything about: sensor noise, tracking, relocalisation, and the
    // frame budget on a phone's CPU. Those need the device.
    {
        auto walk = std::make_shared<VROARWorldMesh>(nullptr);
        walk->setEnabled(true);
        VROWorldMeshConfig wcfg = cfg;
        wcfg.voxelSize = 0.04f;
        walk->setConfig(wcfg);

        std::vector<int> counts;
        const int steps = 60;                       // one per second of the 60 s criterion
        for (int s = 0; s < steps; s++) {
            // Round the room, a metre in from the walls, looking outward.
            const float t = (float)s / steps * 2.0f * (float)M_PI;
            const VROVector3f eye(1.0f * std::cos(t), 0.0f, 1.5f * std::sin(t));
            const VROVector3f fwd(std::cos(t), 0.0f, std::sin(t));

            f.camera->position = eye;
            f.depthMesh = roomPatch(eye, fwd);
            { std::unique_ptr<VROARFrame> frame(&f); walk->updateFromFrame(frame); frame.release(); }
            counts.push_back(walk->getStats().vertexCount);
        }

        // "Roughly monotonic": the fused volume may shed a little to eviction, but it must not fall
        // away with the camera's heading, which is exactly what the single-frame path did.
        int drops = 0;
        for (size_t i = 1; i < counts.size(); i++) {
            if (counts[i] < counts[i - 1] * 0.8) drops++;
        }
        snprintf(buf, sizeof(buf), "(%d drops over 20%% in %zu steps, %d -> %d)",
                 drops, counts.size(), counts.front(), counts.back());
        check("a walk round the room climbs instead of following the view", drops == 0, buf);

        const int finalCount = counts.back();
        snprintf(buf, sizeof(buf), "(%d vertices)", finalCount);
        check("and builds the 20,000 vertices the criteria ask for", finalCount >= 20000, buf);

        // The one VPS Lite uploads, off the same walk.
        std::vector<uint8_t> roomSnap = walk->serializeCurrentMesh(L);
        const uint32_t snapVertices = roomSnap.size() > 13
            ? (uint32_t)(roomSnap[5] | (roomSnap[6] << 8) | (roomSnap[7] << 16) | ((uint32_t)roomSnap[8] << 24))
            : 0;
        snprintf(buf, sizeof(buf), "(%u vertices, %zu bytes)", snapVertices, roomSnap.size());
        check("and the snapshot carries the whole room", snapVertices >= 20000, buf);
    }

    // ── 10. The memory budget is a budget ─────────────────────────────────
    // maxMemoryMB turns into a block count and eviction drops the least recently seen. A room
    // larger than the budget must stay inside it rather than growing until the app is killed.
    {
        const size_t blockBytes = VROTSDFVolume::getBytesPerBlock();
        const size_t budgetBytes = 1u << 20;                    // a deliberately tight 1 MB
        const size_t maxBlocks = budgetBytes / blockBytes;
        VROTSDFVolume vol(0.04f, 0.12f, maxBlocks);

        // Walk a room that needs far more than the budget.
        for (int s = 0; s < 40; s++) {
            const float t = (float)s / 40 * 2.0f * (float)M_PI;
            const VROVector3f eye(1.0f * std::cos(t), 0.0f, 1.5f * std::sin(t));
            auto patch = roomPatch(eye, VROVector3f(std::cos(t), 0.0f, std::sin(t)));
            vol.integrate(patch->getVertices(), patch->getConfidences(), eye);
        }

        snprintf(buf, sizeof(buf), "(%zu blocks of %zu max, %.2f MB of 1.00 MB)",
                 vol.getBlockCount(), maxBlocks, vol.getApproximateBytes() / (1024.0 * 1024.0));
        check("a room larger than the budget stays inside it",
              vol.getBlockCount() <= maxBlocks && vol.getApproximateBytes() <= budgetBytes, buf);
    }

    printf("\n%s  (%d failures)\n", fails == 0 ? "ALL GREEN" : "FAILURES", fails);
    return fails;
}
