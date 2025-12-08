//
//  VROPhysicsShape.m
//  ViroRenderer
//
//  Copyright © 2017 Viro Media. All rights reserved.
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

#include "VROPhysicsShape.h"
#include "VROBox.h"
#include "VROSphere.h"
#include "VROLog.h"
#include "VRONode.h"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

const std::string VROPhysicsShape::kSphereTag = "Sphere";
const std::string VROPhysicsShape::kBoxTag = "Box";
const std::string VROPhysicsShape::kAutoCompoundTag = "Compound";
const std::string VROPhysicsShape::kTriangleMeshTag = "TriangleMesh";
const float kMinBoxSize = 0.001f;

VROPhysicsShape::VROPhysicsShape(VROShapeType type, std::vector<float> params) {
    if (type != VROShapeType::Sphere && type != VROShapeType::Box){
        perror("Attempted to construct unsupported VROPhysicsShape type!");
    }

    if ((type == VROShapeType::Sphere && params.size() < 1)
        || (type == VROShapeType::Box && params.size() < 3)){
        perror("Attempted to construct VROPhysics shape with incorrect parameters!");
    }

    _type = type;
    _bulletShape = generateBasicBulletShape(type, params);
}

VROPhysicsShape::VROPhysicsShape(std::shared_ptr<VRONode> node, bool hasCompoundShapes){
    if (hasCompoundShapes) {
        btCompoundShape* compoundShape = new btCompoundShape();
        generateCompoundBulletShape(*compoundShape, node, node);
        _bulletShape = compoundShape;
        _type = VROShapeType::AutoCompound;
    } else {
        _bulletShape = generateBasicBulletShape(node);
        _type = VROShapeType::Auto;

        VROMatrix4f computedTransform = node->getWorldTransform();
        VROVector3f scale = computedTransform.extractScale();
        _bulletShape->setLocalScaling(btVector3(scale.x, scale.y, scale.z));
    }
}

VROPhysicsShape::VROPhysicsShape(const std::vector<VROVector3f>& vertices,
                                 const std::vector<int>& indices) {
    _type = VROShapeType::TriangleMesh;
    _triangleMesh = nullptr;
    _bulletShape = generateTriangleMeshShape(vertices, indices);
}

VROPhysicsShape::~VROPhysicsShape() {
    if (_bulletShape != nullptr) {
        delete _bulletShape;
        _bulletShape = nullptr;
    }
    // Clean up triangle mesh data (Bullet requires this to persist for shape lifetime)
    if (_triangleMesh != nullptr) {
        delete _triangleMesh;
        _triangleMesh = nullptr;
    }
}

btCollisionShape* VROPhysicsShape::getBulletShape() {
    return _bulletShape;
}

bool VROPhysicsShape::getIsGeneratedFromGeometry() {
    return _type == Auto || _type == AutoCompound;
}

bool VROPhysicsShape::getIsCompoundShape() {
    return _type == AutoCompound;
}

btCollisionShape* VROPhysicsShape::generateBasicBulletShape(std::shared_ptr<VRONode> node) {
    if (node->getGeometry() == nullptr) {
        pwarn("Warn: Attempted to create a physics shape from a node without defined geometry!");
        return nullptr;
    }

    std::shared_ptr<VROGeometry> geometry = node->getGeometry();
    std::vector<float> params;
    VROPhysicsShape::VROShapeType type;
    if (dynamic_cast<VROSphere*>(geometry.get()) != nullptr) {
        type = VROPhysicsShape::VROShapeType::Sphere;
        // Grab the max span to account for skewed spheres - we simply
        // assume a perfect sphere for these situations.
        VROBoundingBox bb = geometry->getBoundingBox();
        float maxSpan = std::max(std::max(bb.getSpanX(), bb.getSpanY()), bb.getSpanZ());
        params.push_back(maxSpan/2);
    } else {
        type = VROPhysicsShape::VROShapeType::Box;
        VROBoundingBox bb = geometry->getBoundingBox();
        params.push_back(std::max(bb.getSpanX(), kMinBoxSize));
        params.push_back(std::max(bb.getSpanY(), kMinBoxSize));
        params.push_back(std::max(bb.getSpanZ(), kMinBoxSize));
    }

    return generateBasicBulletShape(type, params);
}

btCollisionShape* VROPhysicsShape::generateBasicBulletShape(VROPhysicsShape::VROShapeType type, std::vector<float> params) {
    if (type == VROPhysicsShape::VROShapeType::Box) {
        return new btBoxShape(btVector3(params[0] / 2, params[1] / 2, params[2] / 2));
    } else if (type == VROPhysicsShape::VROShapeType::Sphere) {
        return new btSphereShape(btScalar(params[0]));
    } else if (type != VROPhysicsShape::VROShapeType::Auto &&
               type != VROPhysicsShape::VROShapeType::AutoCompound) {
        perror("Attempted to grab a bullet shape from a mis-configured VROPhysicsShape!");
    }
    return nullptr;
}

void VROPhysicsShape::generateCompoundBulletShape(btCompoundShape &compoundShape,
                                                  const std::shared_ptr<VRONode> &rootNode,
                                                  const std::shared_ptr<VRONode> &currentNode) {
    btCollisionShape* shape = generateBasicBulletShape(currentNode);
    if (shape != nullptr) {
        // Bullet requires a flat structure when creating a compoundShape.
        // To achieve this, we transform each node such that they are oriented in
        // relation to the rootNode (as if the rootNode is were the origin).
        VROMatrix4f rootTransformInverted = rootNode->getWorldTransform().invert();
        VROMatrix4f currentNodeTransform = currentNode->getWorldTransform();
        VROMatrix4f currentShapeTransform = rootTransformInverted * currentNodeTransform;

        VROVector3f pos = currentShapeTransform.extractTranslation();
        VROVector3f scale = currentShapeTransform.extractScale();
        VROQuaternion rot = currentShapeTransform.extractRotation(scale);

        btTransform curentShapeTransformBullet;
        curentShapeTransformBullet.setIdentity();
        curentShapeTransformBullet.setOrigin({pos.x, pos.y, pos.z});
        curentShapeTransformBullet.setRotation({rot.X, rot.Y, rot.Z, rot.W});

        // Note: manually apply the scale of the rootNode (compound node) across
        // the list of sub shapes that we add. This is because there is a bug
        // in the function call of bulletShape->setLocalScaling.
        VROVector3f compoundScale = rootNode->getWorldTransform().extractScale();
        btVector3 compoundScaleBullet = btVector3({compoundScale.x, compoundScale.y , compoundScale.z});
        btVector3 currentShapeScaleBullet = btVector3({scale.x, scale.y , scale.z});
        btMatrix3x3 transformBasis = curentShapeTransformBullet.getBasis();
        currentShapeScaleBullet = currentShapeScaleBullet * (transformBasis * compoundScaleBullet);
        shape->setLocalScaling(currentShapeScaleBullet);
        curentShapeTransformBullet.setOrigin({pos.x * compoundScale.x ,
                                              pos.y * compoundScale.y ,
                                              pos.z * compoundScale.z });

        compoundShape.addChildShape(curentShapeTransformBullet, shape);
    }

    // Recurse for all child nodes.
    const std::vector<std::shared_ptr<VRONode>> subNodes = currentNode->getChildNodes();
    for(std::shared_ptr<VRONode> node: subNodes) {
        generateCompoundBulletShape(compoundShape, rootNode, node);
    }
}

btCollisionShape* VROPhysicsShape::generateTriangleMeshShape(
    const std::vector<VROVector3f>& vertices,
    const std::vector<int>& indices)
{
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) {
        pwarn("Invalid mesh data for triangle mesh shape: vertices=%zu, indices=%zu",
              vertices.size(), indices.size());
        return nullptr;
    }

    // Create Bullet triangle mesh (must persist for lifetime of shape)
    _triangleMesh = new btTriangleMesh();

    int trianglesAdded = 0;

    // Add triangles to the mesh
    for (size_t i = 0; i < indices.size(); i += 3) {
        int i0 = indices[i];
        int i1 = indices[i + 1];
        int i2 = indices[i + 2];

        // Validate indices
        if (i0 < 0 || i0 >= static_cast<int>(vertices.size()) ||
            i1 < 0 || i1 >= static_cast<int>(vertices.size()) ||
            i2 < 0 || i2 >= static_cast<int>(vertices.size())) {
            continue;
        }

        const VROVector3f& v0 = vertices[i0];
        const VROVector3f& v1 = vertices[i1];
        const VROVector3f& v2 = vertices[i2];

        // Skip degenerate triangles
        VROVector3f edge1 = v1 - v0;
        VROVector3f edge2 = v2 - v0;
        if (edge1.cross(edge2).magnitude() < 0.0001f) {
            continue;
        }

        _triangleMesh->addTriangle(
            btVector3(v0.x, v0.y, v0.z),
            btVector3(v1.x, v1.y, v1.z),
            btVector3(v2.x, v2.y, v2.z),
            true  // Remove duplicate vertices
        );
        trianglesAdded++;
    }

    if (trianglesAdded == 0) {
        pwarn("No valid triangles in mesh data");
        delete _triangleMesh;
        _triangleMesh = nullptr;
        return nullptr;
    }

    // Create BVH triangle mesh shape (optimized for static collision geometry)
    // BVH (Bounding Volume Hierarchy) provides efficient collision detection
    bool useQuantizedAabbCompression = true;
    btBvhTriangleMeshShape* meshShape = new btBvhTriangleMeshShape(
        _triangleMesh,
        useQuantizedAabbCompression
    );

    pinfo("Created triangle mesh physics shape with %d triangles", trianglesAdded);

    return meshShape;
}
