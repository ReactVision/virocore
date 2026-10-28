// Dobles para las partes del renderer que el camino de fusión no ejerce: dibujo de depuración,
// geometría de oclusión y el grafo de escena. El VROARWorldMesh del test se construye con
// physicsWorld nulo, así que las rutinas de física tampoco llegan a hacer nada observable.
#include <memory>
#include <vector>
#include <string>
#include "VROVector3f.h"
#include "VROBoundingBox.h"
#include "VROData.h"
#include "VROMaterial.h"
#include "VRONode.h"
#include "VROPencil.h"
#include "VROScene.h"
#include "VROPhysicsShape.h"
#include "VROPhysicsWorld.h"
#include "VROGeometry.h"
#include "VROThreadRestricted.h"
#include "VROPortal.h"

VROBoundingBox::VROBoundingBox() noexcept {}
VROData::VROData(void*, int, VRODataOwnership) {}
VROData::~VROData() {}
VROMaterial::VROMaterial() : VROThreadRestricted(VROThreadName::Renderer) {}
void VROMaterial::updateSubstrate() {}
VRONode::VRONode() : VROThreadRestricted(VROThreadName::Renderer) {}
void VRONode::addChildNode(std::shared_ptr<VRONode>) {}
void VROPencil::draw(VROVector3f, VROVector3f) {}
VROPhysicsShape::VROPhysicsShape(const std::vector<VROVector3f>&, const std::vector<int>&) {}
btCollisionShape *VROPhysicsShape::getBulletShape() { return nullptr; }
void VROPhysicsWorld::addRigidBody(btRigidBody*) {}
void VROPhysicsWorld::removeRigidBody(btRigidBody*) {}
std::shared_ptr<VROPortal> VROScene::getRootNode() { return nullptr; }
std::vector<std::shared_ptr<VROGeometrySource>> VROShapeUtilBuildGeometrySources(std::shared_ptr<VROData>, size_t) { return {}; }
void VROThreadRestricted::passert_thread(std::string) {}

// El resto del cierre: constructores base y las vtables que el enlazador exige aunque el test
// nunca instancie estas clases. Un método virtual definido ancla cada vtable.
#include "VROFrustumBoxIntersectionMetadata.h"
VROFrustumBoxIntersectionMetadata::VROFrustumBoxIntersectionMetadata() {}
VROFrustumBoxIntersectionMetadata::~VROFrustumBoxIntersectionMetadata() {}
VROThreadRestricted::VROThreadRestricted(VROThreadName n) : _restricted_thread_name(n) {}
VROThreadRestricted::~VROThreadRestricted() {}
const std::string kDefaultNodeTag = "node";

// Destructores: anclan la vtable de cada clase, que el enlazador exige aunque el test no las use.
VROGeometry::~VROGeometry() {}
VROMaterial::~VROMaterial() {}
VRONode::~VRONode() {}
VROPhysicsShape::~VROPhysicsShape() {}
void VRONode::deleteGL() {}
void VRONode::onAnimationFinished() {}
void VRONode::setHidden(bool) {}
