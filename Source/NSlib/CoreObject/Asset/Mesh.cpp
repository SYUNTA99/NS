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
    {}

    Mesh::Mesh(std::string path,
               std::unique_ptr<Gfx::D3DSkeletalMesh> mesh,
               Anim::Skeleton skeleton,
               std::vector<BoneSphere> boneSpheres,
               MeshUnsetMaterial unset) noexcept
        : m_path(std::move(path)), m_mesh(std::move(mesh)), m_skeleton(std::move(skeleton)),
          m_boneSpheres(std::move(boneSpheres)), m_unset(unset)
    {}

    Mesh::~Mesh() noexcept = default;

    const std::string& Mesh::Path() const noexcept
    {
        return m_path;
    }

    Gfx::D3DMesh* Mesh::D3D() const noexcept
    {
        return m_mesh.get();
    }

    Phys::MeshShape* Mesh::Collision() const noexcept
    {
        return m_collision.get();
    }

    const Anim::Skeleton* Mesh::Skeleton() const noexcept
    {
        if (m_skeleton.BoneCount() == 0u)
        {
            return nullptr;
        }
        return &m_skeleton;
    }

    const std::vector<BoneSphere>& Mesh::BoneSpheres() const noexcept
    {
        return m_boneSpheres;
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
        return m_mesh == nullptr && m_collision == nullptr && m_skeleton.BoneCount() == 0u;
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
