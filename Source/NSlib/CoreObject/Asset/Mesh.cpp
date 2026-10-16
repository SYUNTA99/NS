#include "NSlib/CoreObject/Asset/Mesh.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Graphics/D3DMesh.h"
#include "NSlib/Graphics/D3DSkeletalMesh.h"
#include "NSlib/Physics/MeshShape.h"

#include <utility>

namespace NS
{
    Mesh::Mesh(std::string path,
               std::unique_ptr<Gfx::D3DMesh> mesh,
               std::unique_ptr<Phys::MeshShape> collision) noexcept
        : m_path(std::move(path)), m_mesh(std::move(mesh)), m_collision(std::move(collision))
    {
    }

    Mesh::Mesh(std::string path, std::unique_ptr<Gfx::D3DSkeletalMesh> mesh) noexcept : m_path(std::move(path))
    {
        m_skeletal = mesh.get();
        m_mesh = std::move(mesh);
    }

    Mesh::~Mesh() noexcept = default;

    const std::string& Mesh::Path() const noexcept
    {
        return m_path;
    }

    Gfx::D3DMesh* Mesh::D3D() const noexcept
    {
        if (m_mesh != nullptr)
        {
            return m_mesh.get();
        }
        if (m_standIn != nullptr)
        {
            return m_standIn->D3D();
        }
        return nullptr;
    }

    Gfx::D3DSkeletalMesh* Mesh::D3DSkeletal() const noexcept
    {
        return m_skeletal;
    }

    Phys::MeshShape* Mesh::Collision() const noexcept
    {
        return m_collision.get();
    }

    const AABB* Mesh::LocalBounds() const noexcept
    {
        const Gfx::D3DMesh* mesh = D3D();
        if (mesh == nullptr)
        {
            return nullptr;
        }
        return &mesh->LocalBounds();
    }

    bool Mesh::UsesStandIn() const noexcept
    {
        return m_mesh == nullptr && m_standIn != nullptr;
    }

    void Mesh::SetD3D(std::unique_ptr<Gfx::D3DMesh> mesh) noexcept
    {
        NS_ASSERT(Scene, m_skeletal == nullptr, "骨付きのメッシュの描く形を入れ替えようとしている");
        m_mesh = std::move(mesh);
    }

    void Mesh::SetStandIn(const Mesh* standIn) noexcept
    {
        NS_ASSERT(Scene, standIn != this, "メッシュの代わりに自分を指そうとしている");
        m_standIn = standIn;
    }
} // namespace NS
