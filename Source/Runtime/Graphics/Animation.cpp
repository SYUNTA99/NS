#include "Runtime/Graphics/Animation.h"

#include <algorithm>
#include <span>

namespace NS::Graphics
{
    namespace
    {
        struct Segment
        {
            std::size_t i0;
            std::size_t i1;
            float f;
        };

        // times は昇順前提・ 非空前提。 t を含む区間 [i0, i1] と補間係数 f を返す
        // 範囲外は端点へクランプし i0==i1, f=0 になる
        [[nodiscard]] Segment FindSegment(std::span<const float> times, float t) noexcept
        {
            const std::size_t n = times.size();
            if (n == 1 || t <= times[0])
            {
                return {0, 0, 0.0f};
            }
            if (t >= times[n - 1])
            {
                return {n - 1, n - 1, 0.0f};
            }
            std::size_t i = 0;
            while (i + 1 < n && times[i + 1] <= t)
            {
                ++i;
            }
            const float span = times[i + 1] - times[i];
            const float f = [&]() -> float {
                if (span > 0.0f)
                    return (t - times[i]) / span;
                return 0.0f;
            }();
            return {i, i + 1, f};
        }
    } // namespace

    NS::Math::Vector3 SampleVec3(std::span<const float> times,
                                 std::span<const NS::Math::Vector3> values,
                                 Interpolation interp,
                                 float t,
                                 const NS::Math::Vector3& fallback) noexcept
    {
        if (times.empty() || values.empty())
        {
            return fallback;
        }
        const Segment seg = FindSegment(times, t);
        const std::size_t i0 = std::min(seg.i0, values.size() - 1);
        const std::size_t i1 = std::min(seg.i1, values.size() - 1);
        if (interp == Interpolation::Step || i0 == i1)
        {
            return values[i0];
        }
        return NS::Math::Vector3::Lerp(values[i0], values[i1], seg.f);
    }

    NS::Math::Quaternion SampleQuat(std::span<const float> times,
                                    std::span<const NS::Math::Quaternion> values,
                                    Interpolation interp,
                                    float t,
                                    const NS::Math::Quaternion& fallback) noexcept
    {
        if (times.empty() || values.empty())
        {
            return fallback;
        }
        const Segment seg = FindSegment(times, t);
        const std::size_t i0 = std::min(seg.i0, values.size() - 1);
        const std::size_t i1 = std::min(seg.i1, values.size() - 1);
        if (interp == Interpolation::Step || i0 == i1)
        {
            return values[i0];
        }
        NS::Math::Quaternion a = values[i0];
        NS::Math::Quaternion b = values[i1];
        // 内積が負なら一方を反転して同じ回転の近い表現へ揃える
        if (a.Dot(b) < 0.0f)
        {
            b = NS::Math::Quaternion{-b.x, -b.y, -b.z, -b.w};
        }
        return NS::Math::Quaternion::Slerp(a, b, seg.f);
    }

    void SampleClipPose(const AnimationClip& clip, const Skeleton& skeleton, float t, std::vector<BonePose>& outPose)
    {
        const std::vector<Bone>& bones = skeleton.Bones();
        outPose.resize(bones.size());
        for (std::size_t i = 0; i < bones.size(); ++i)
        {
            outPose[i] = bones[i].bindLocal;
        }

        for (const BoneTrack& track : clip.tracks)
        {
            if (track.boneIndex < 0)
            {
                continue;
            }
            const std::size_t boneIndex = static_cast<std::size_t>(track.boneIndex);
            if (boneIndex >= outPose.size())
            {
                continue;
            }
            BonePose& pose = outPose[boneIndex];
            pose.translation =
                SampleVec3(track.positionTimes, track.positionValues, track.positionInterp, t, pose.translation);
            pose.rotation =
                SampleQuat(track.rotationTimes, track.rotationValues, track.rotationInterp, t, pose.rotation);
            pose.scale = SampleVec3(track.scaleTimes, track.scaleValues, track.scaleInterp, t, pose.scale);
        }
    }

} // namespace NS::Graphics
