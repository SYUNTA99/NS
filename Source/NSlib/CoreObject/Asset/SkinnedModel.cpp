#include "NSlib/CoreObject/Asset/SkinnedModel.h"

#include "NSlib/CoreObject/Asset/Mesh.h"

#include <utility>

namespace NS
{
    SkinnedModel::SkinnedModel(std::string path) noexcept : m_path(std::move(path)) {}

    SkinnedModel::SkinnedModel(std::string path, std::unique_ptr<Mesh> mesh) noexcept
        : m_path(std::move(path)), m_mesh(std::move(mesh))
    {
    }

    SkinnedModel::~SkinnedModel() noexcept = default;

    const std::string& SkinnedModel::Path() const noexcept { return m_path; }

    Mesh* SkinnedModel::GetMesh() const noexcept { return m_mesh.get(); }

    const Anim::Skeleton* SkinnedModel::GetSkeleton() const noexcept
    {
        if (m_mesh == nullptr)
        {
            return nullptr;
        }
        return m_mesh->Skeleton();
    }
} // namespace NS
