#include "Framework/Graphics/Skeleton.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <utility>

namespace NS::Graphics
{
    namespace
    {
        [[nodiscard]] NS::Math::Matrix LocalMatrix(const BonePose& pose) noexcept
        {
            const NS::Math::Matrix scale = NS::Math::Matrix::CreateScale(pose.scale);
            const NS::Math::Matrix rotate = NS::Math::Matrix::CreateFromQuaternion(pose.rotation);
            const NS::Math::Matrix translate = NS::Math::Matrix::CreateTranslation(pose.translation);
            return scale * rotate * translate;
        }
    } // namespace

    Skeleton::Skeleton(std::vector<Bone> bones) noexcept : m_bones(std::move(bones)) {}

    std::size_t Skeleton::BoneCount() const noexcept
    {
        return m_bones.size();
    }

    const std::vector<Bone>& Skeleton::Bones() const noexcept
    {
        return m_bones;
    }

    void Skeleton::ComputePalette(std::span<const BonePose> pose, std::vector<NS::Math::Matrix>& out) const
    {
        const std::size_t boneCount = m_bones.size();
        out.assign(boneCount, NS::Math::Matrix::Identity);
        if (pose.size() != boneCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Skeleton::ComputePalette: pose 数 ({}) が bone 数 ({}) と不一致",
                         pose.size(),
                         boneCount);
            return;
        }

        std::vector<NS::Math::Matrix> jointWorld(boneCount, NS::Math::Matrix::Identity);
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            const NS::Math::Matrix local = LocalMatrix(pose[i]);
            const int parent = m_bones[i].parentIndex;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
            {
                jointWorld[i] = local * jointWorld[parent];
            }
            else
            {
                // root、 または topological 順を満たさない前方参照は root 扱いに落とす
                // root には skeleton 上位ノード変換 (アーマチュア) を親ワールドとして掛ける
                jointWorld[i] = local * m_rootTransform;
            }
            out[i] = m_bones[i].inverseBind * jointWorld[i];
        }
    }

    void Skeleton::ComputeGlobals(std::span<const BonePose> pose,
                                  std::vector<NS::Math::Matrix>& out,
                                  bool applyRootTransform) const
    {
        const std::size_t boneCount = m_bones.size();
        out.assign(boneCount, NS::Math::Matrix::Identity);
        if (pose.size() != boneCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Skeleton::ComputeGlobals: pose 数 ({}) が bone 数 ({}) と不一致",
                         pose.size(),
                         boneCount);
            return;
        }

        const NS::Math::Matrix rootParent = applyRootTransform ? m_rootTransform : NS::Math::Matrix::Identity;
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            const NS::Math::Matrix local = LocalMatrix(pose[i]);
            const int parent = m_bones[i].parentIndex;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
                out[i] = local * out[parent];
            else
                out[i] = local * rootParent;
        }
    }

    void Skeleton::ComputeBindPalette(std::vector<NS::Math::Matrix>& out) const
    {
        std::vector<BonePose> bindPose;
        bindPose.reserve(m_bones.size());
        for (const Bone& bone : m_bones)
        {
            bindPose.push_back(bone.bindLocal);
        }
        ComputePalette(bindPose, out);
    }

    void Skeleton::SetRootTransform(const NS::Math::Matrix& transform) noexcept
    {
        m_rootTransform = transform;
    }

    const NS::Math::Matrix& Skeleton::RootTransform() const noexcept
    {
        return m_rootTransform;
    }

    NS::Math::Vector3 Skeleton::SkinPositionReference(const SkinnedVertex& vertex,
                                                      std::span<const NS::Math::Matrix> palette) noexcept
    {
        NS::Math::Vector3 result(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            const float weight = vertex.weights[i];
            if (weight == 0.0f)
            {
                continue;
            }
            const std::uint32_t joint = vertex.joints[i];
            if (joint >= palette.size())
            {
                continue;
            }
            result += weight * NS::Math::Vector3::Transform(vertex.position, palette[joint]);
        }
        return result;
    }

} // namespace NS::Graphics
