#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"

#include <span>

namespace NS
{
    namespace Anim
    {
        namespace
        {
            [[nodiscard]] Matrix LocalMatrix(const BonePose& pose) noexcept
            {
                const Matrix scale = Matrix::CreateScale(pose.scale);
                const Matrix rotate = Matrix::CreateFromQuaternion(pose.rotation);
                const Matrix translate = Matrix::CreateTranslation(pose.translation);
                return scale * rotate * translate;
            }
        } // namespace

        void BlendPose(std::span<const BonePose> from,
                       std::span<const BonePose> to,
                       float weight,
                       std::vector<BonePose>& out)
        {
            if (from.size() != to.size())
            {
                NS_LOG_ERROR(Graphics,
                             "BlendPose: 元の姿勢の骨の数 ({}) が次の姿勢の骨の数 ({}) と不一致",
                             from.size(),
                             to.size());
                out.assign(from.begin(), from.end());
                return;
            }

            out.resize(from.size());
            const float t = Clamp(weight, 0.0f, 1.0f);
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                out[i].translation = Vector3::Lerp(from[i].translation, to[i].translation, t);
                // XMQuaternionSlerp は内積が負の時に符号を反転するので、短い方を回る
                out[i].rotation = Quaternion::Slerp(from[i].rotation, to[i].rotation, t);
                out[i].scale = Vector3::Lerp(from[i].scale, to[i].scale, t);
            }
        }

        Skeleton::Skeleton(std::vector<Bone> bones) noexcept : m_bones(std::move(bones)) {}

        std::size_t Skeleton::BoneCount() const noexcept
        {
            return m_bones.size();
        }

        const std::vector<Bone>& Skeleton::Bones() const noexcept
        {
            return m_bones;
        }

        void Skeleton::ComputePalette(std::span<const BonePose> pose, std::vector<Matrix>& out) const
        {
            const std::size_t boneCount = m_bones.size();
            out.assign(boneCount, Matrix::Identity);
            if (pose.size() != boneCount)
            {
                NS_LOG_ERROR(Graphics,
                             "Skeleton::ComputePalette: pose 数 ({}) が bone 数 ({}) と不一致",
                             pose.size(),
                             boneCount);
                return;
            }

            ComputeGlobals(pose, out, true);
            for (std::size_t i = 0; i < boneCount; ++i)
            {
                out[i] = m_bones[i].inverseBind * out[i];
            }
        }

        void Skeleton::ComputeGlobals(std::span<const BonePose> pose,
                                      std::vector<Matrix>& out,
                                      bool applyRootTransform) const
        {
            const std::size_t boneCount = m_bones.size();
            out.assign(boneCount, Matrix::Identity);
            if (pose.size() != boneCount)
            {
                NS_LOG_ERROR(Graphics,
                             "Skeleton::ComputeGlobals: pose 数 ({}) が bone 数 ({}) と不一致",
                             pose.size(),
                             boneCount);
                return;
            }

            const Matrix rootParent = [&]() -> Matrix {
                if (applyRootTransform)
                {
                    return m_rootTransform;
                }
                return Matrix::Identity;
            }();
            for (std::size_t i = 0; i < boneCount; ++i)
            {
                const Matrix local = LocalMatrix(pose[i]);
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

        void Skeleton::SetRootTransform(const Matrix& transform) noexcept
        {
            m_rootTransform = transform;
        }

        const Matrix& Skeleton::RootTransform() const noexcept
        {
            return m_rootTransform;
        }

        Vector3 Skeleton::SkinPositionReference(const SkinnedVertex& vertex,
                                                    std::span<const Matrix> palette) noexcept
        {
            Vector3 result(0.0f, 0.0f, 0.0f);
            for (int i = 0; i < 4; ++i)
            {
                const float weight = vertex.weights[i];
                if (weight == 0.0f)
                {
                    continue;
                }
                const uint32 joint = vertex.joints[i];
                if (joint >= palette.size())
                {
                    continue;
                }
                result += weight * Vector3::Transform(vertex.position, palette[joint]);
            }
            return result;
        }
    } // namespace Anim
} // namespace NS
