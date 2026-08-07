#include "Runtime/Graphics/Skeleton.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <span>

namespace NS::Graphics
{
    namespace
    {
        [[nodiscard]] NS::Core::Matrix LocalMatrix(const BonePose& pose) noexcept
        {
            const NS::Core::Matrix scale = NS::Core::Matrix::CreateScale(pose.scale);
            const NS::Core::Matrix rotate = NS::Core::Matrix::CreateFromQuaternion(pose.rotation);
            const NS::Core::Matrix translate = NS::Core::Matrix::CreateTranslation(pose.translation);
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

    void Skeleton::ComputePalette(std::span<const BonePose> pose, std::vector<NS::Core::Matrix>& out) const
    {
        const std::size_t boneCount = m_bones.size();
        out.assign(boneCount, NS::Core::Matrix::Identity);
        if (pose.size() != boneCount)
        {
            NS_LOG_ERROR(
                Graphics, "Skeleton::ComputePalette: pose 数 ({}) が bone 数 ({}) と不一致", pose.size(), boneCount);
            return;
        }

        std::vector<NS::Core::Matrix> jointWorld(boneCount, NS::Core::Matrix::Identity);
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            const NS::Core::Matrix local = LocalMatrix(pose[i]);
            const int parent = m_bones[i].parentIndex;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
            {
                jointWorld[i] = local * jointWorld[parent];
            }
            else
            {
                // 根本となるボーンには、スケルトン全体の基準となる変換行列を適用する
                jointWorld[i] = local * m_rootTransform;
            }
            out[i] = m_bones[i].inverseBind * jointWorld[i];
        }
    }

    void Skeleton::ComputeGlobals(std::span<const BonePose> pose,
                                  std::vector<NS::Core::Matrix>& out,
                                  bool applyRootTransform) const
    {
        const std::size_t boneCount = m_bones.size();
        out.assign(boneCount, NS::Core::Matrix::Identity);
        if (pose.size() != boneCount)
        {
            NS_LOG_ERROR(
                Graphics, "Skeleton::ComputeGlobals: pose 数 ({}) が bone 数 ({}) と不一致", pose.size(), boneCount);
            return;
        }

        const NS::Core::Matrix rootParent = [&]() -> NS::Core::Matrix {
            if (applyRootTransform)
            {
                return m_rootTransform;
            }
            return NS::Core::Matrix::Identity;
        }();
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            const NS::Core::Matrix local = LocalMatrix(pose[i]);
            const int parent = m_bones[i].parentIndex;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
                out[i] = local * out[parent];
            else
                out[i] = local * rootParent;
        }
    }

    void Skeleton::ComputeBindPalette(std::vector<NS::Core::Matrix>& out) const
    {
        std::vector<BonePose> bindPose;
        bindPose.reserve(m_bones.size());
        for (const Bone& bone : m_bones)
        {
            bindPose.push_back(bone.bindLocal);
        }
        ComputePalette(bindPose, out);
    }

    void Skeleton::SetRootTransform(const NS::Core::Matrix& transform) noexcept
    {
        m_rootTransform = transform;
    }

    const NS::Core::Matrix& Skeleton::RootTransform() const noexcept
    {
        return m_rootTransform;
    }

    NS::Core::Vector3 Skeleton::SkinPositionReference(const SkinnedVertex& vertex,
                                                      std::span<const NS::Core::Matrix> palette) noexcept
    {
        NS::Core::Vector3 result(0.0f, 0.0f, 0.0f);
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
            result += weight * NS::Core::Vector3::Transform(vertex.position, palette[joint]);
        }
        return result;
    }

} // namespace NS::Graphics
