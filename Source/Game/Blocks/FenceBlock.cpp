#include "Game/Blocks/FenceBlock.h"

FenceBlock::FenceBlock(NS::Graphics::Mesh* quadMesh,
                       NS::Graphics::Material* material,
                       const NS::Core::Vector3& halfExtents,
                       const NS::Core::Vector3& faceNormal) noexcept
    : m_mesh(this, quadMesh, material), m_climbable(this, NS::Scene::ClimbableKind::Fence, halfExtents, faceNormal)
{}
