#include "NSlib/CoreObject/Asset/Mesh.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Graphics/D3DMesh.h"
#include "NSlib/Graphics/D3DSkeletalMesh.h"
#include "NSlib/Physics/MeshShape.h"

#include <utility>

namespace NS
{
    Mesh::Mesh(std::unique_ptr<Gfx::D3DMesh> mesh, std::unique_ptr<Phys::MeshShape> collision) noexcept
        : m_mesh(std::move(mesh)), m_collision(std::move(collision))
    {
    }

    Mesh::Mesh(std::unique_ptr<Gfx::D3DSkeletalMesh> mesh) noexcept
    {
        m_skeletal = mesh.get();
        m_mesh = std::move(mesh);
    }

    Mesh::~Mesh() noexcept = default;

    Gfx::D3DMesh* Mesh::D3D() const noexcept
    {
        return m_mesh.get();
    }

    Gfx::D3DSkeletalMesh* Mesh::D3DSkeletal() const noexcept
    {
        return m_skeletal;
    }

    Phys::MeshShape* Mesh::Collision() const noexcept
    {
        return m_collision.get();
    }

    void Mesh::SetD3D(std::unique_ptr<Gfx::D3DMesh> mesh) noexcept
    {
        NS_ASSERT(Scene, m_skeletal == nullptr, "骨付きのメッシュの描く形を入れ替えようとしている");
        m_mesh = std::move(mesh);
    }
} // namespace NS
