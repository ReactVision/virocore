// Bullet solo está compilado para el simulador x86_64 en este repo, y la fusión no necesita
// física: el VROARWorldMesh del test se construye con physicsWorld nulo, así que el cuerpo rígido
// se crea y se destruye sin entrar nunca en un mundo de física.
#include <cstdlib>
#include <cstring>
#include "btBulletDynamicsCommon.h"

void *btAlignedAllocInternal(size_t size, int alignment) {
    void *p = nullptr;
    if (posix_memalign(&p, alignment < 16 ? 16 : (size_t)alignment, size ? size : 1) != 0) return nullptr;
    memset(p, 0, size);
    return p;
}
void btAlignedFreeInternal(void *ptr) { free(ptr); }

btRigidBody::btRigidBody(const btRigidBody::btRigidBodyConstructionInfo &info) {
    m_collisionShape = info.m_collisionShape;
    m_internalType = CO_RIGID_BODY;
    m_collisionFlags = 0;
    m_userObjectPointer = nullptr;
    m_userIndex = -1;
    m_worldTransform.setIdentity();
}

btCollisionObject::btCollisionObject() {
    m_worldTransform.setIdentity();
    m_interpolationWorldTransform.setIdentity();
    m_collisionShape = nullptr;
    m_rootCollisionShape = nullptr;
    m_collisionFlags = 0;
    m_islandTag1 = -1;
    m_companionId = -1;
    m_worldArrayIndex = -1;
    m_activationState1 = 1;
    m_internalType = CO_COLLISION_OBJECT;
    m_userObjectPointer = nullptr;
    m_userIndex = -1;
    m_userIndex2 = -1;
    m_updateRevision = 0;
}
btCollisionObject::~btCollisionObject() {}

// Ancla la vtable de btRigidBody: el test nunca simula, solo construye y destruye el cuerpo.
void btRigidBody::setupRigidBody(const btRigidBody::btRigidBodyConstructionInfo &) {}
const char *btCollisionObject::serialize(void *, btSerializer *) const { return "btCollisionObjectDoubleData"; }
void btCollisionObject::serializeSingleObject(btSerializer *) const {}
int  btRigidBody::calculateSerializeBufferSize() const { return 0; }
const char *btRigidBody::serialize(void *, btSerializer *) const { return "btRigidBodyDoubleData"; }
void btRigidBody::serializeSingleObject(btSerializer *) const {}
