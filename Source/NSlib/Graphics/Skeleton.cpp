#include "NSlib/Graphics/Skeleton.h"

#include "NSlib/Core/Logger.h"

#include <span>

namespace NS::Gfx
{
    namespace
    {
        [[nodiscard]] NS::Matrix LocalMatrix(const BonePose& pose) noexcept
        {
            const NS::Matrix scale = NS::Matrix::CreateScale(pose.scale);
            const NS::Matrix rotate = NS::Matrix::CreateFromQuaternion(pose.rotation);
            const NS::Matrix translate = NS::Matrix::CreateTranslation(pose.translation);
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

    void Skeleton::ComputePalette(std::span<const BonePose> pose, std::vector<NS::Matrix>& out) const
    {
        const std::size_t boneCount = m_bones.size();
        out.assign(boneCount, NS::Matrix::Identity);
        if (pose.size() != boneCount)
        {
            NS_LOG_ERROR(
                Graphics, "Skeleton::ComputePalette: pose 数 ({}) が bone 数 ({}) と不一致", pose.size(), boneCount);
            return;
        }

        // 根本となるボーンには、スケルトン全体の基準となる変換行列を適用する
        ComputeGlobals(pose, out, true);
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            out[i] = m_bones[i].inverseBind * out[i];
        }
    }

    void Skeleton::ComputeGlobals(std::span<const BonePose> pose,
                                  std::vector<NS::Matrix>& out,
                                  bool applyRootTransform) const
    {
        const std::size_t boneCount = m_bones.size();
        out.assign(boneCount, NS::Matrix::Identity);
        if (pose.size() != boneCount)
        {
            NS_LOG_ERROR(
                Graphics, "Skeleton::ComputeGlobals: pose 数 ({}) が bone 数 ({}) と不一致", pose.size(), boneCount);
            return;
        }

        const NS::Matrix rootParent = [&]() -> NS::Matrix {
            if (applyRootTransform)
            {
                return m_rootTransform;
            }
            return NS::Matrix::Identity;
        }();
        for (std::size_t i = 0; i < boneCount; ++i)
        {
            const NS::Matrix local = LocalMatrix(pose[i]);
            const int parent = m_bones[i].parentIndex;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
            {
                out[i] = local * out[parent];
            }
            else
            {
                out[i] = local * rootParent;
            }
        }
    }

    void Skeleton::ComputeBindPalette(std::vector<NS::Matrix>& out) const
    {
        std::vector<BonePose> bindPose;
        bindPose.reserve(m_bones.size());
        for (const Bone& bone : m_bones)
        {
            bindPose.push_back(bone.bindLocal);
        }
        ComputePalette(bindPose, out);
    }

    void Skeleton::SetRootTransform(const NS::Matrix& transform) noexcept
    {
        m_rootTransform = transform;
    }

    const NS::Matrix& Skeleton::RootTransform() const noexcept
    {
        return m_rootTransform;
    }

    NS::Vector3 Skeleton::SkinPositionReference(const SkinnedVertex& vertex,
                                                      std::span<const NS::Matrix> palette) noexcept
    {
        NS::Vector3 result(0.0f, 0.0f, 0.0f);
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
            result += weight * NS::Vector3::Transform(vertex.position, palette[joint]);
        }
        return result;
    }

} // namespace NS::Gfx
