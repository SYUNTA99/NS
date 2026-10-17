#include "NSlib/CoreObject/Asset/Mesh.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DSkeletalMesh.h"
#include "NSlib/Phys/MeshShape.h"

#include <utility>

namespace NS
{
    Mesh::Mesh(std::string path,
               std::unique_ptr<Gfx::D3DMesh> mesh,
               std::unique_ptr<Phys::MeshShape> collision,
               MeshUnsetMaterial unset) noexcept
        : m_path(std::move(path)), m_mesh(std::move(mesh)), m_collision(std::move(collision)), m_unset(unset)
    {
    }

    Mesh::Mesh(std::string path, std::unique_ptr<Gfx::D3DSkeletalMesh> mesh, MeshUnsetMaterial unset) noexcept
        : m_path(std::move(path)), m_unset(unset)
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

    const AABB* Mesh::LocalBounds() const noexcept
    {
        const Gfx::D3DMesh* mesh = D3D();
        if (mesh == nullptr)
        {
            return nullptr;
        }
        return &mesh->LocalBounds();
    }

    bool Mesh::IsEmpty() const noexcept
    {
        return m_mesh == nullptr && m_collision == nullptr;
    }

    Material* Mesh::UseUnsetMaterial()
    {
        if (m_unset.reportWhenUsed)
        {
            NS_LOG_ERROR(Scene, "メッシュ {} にマテリアルが無いので、ピンクで描く", m_path);
            m_unset.reportWhenUsed = false;
        }
        return m_unset.material;
    }
} // namespace NS
