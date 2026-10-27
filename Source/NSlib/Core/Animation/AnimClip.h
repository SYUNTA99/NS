#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Animation/Skeleton.h"

#include <span>
#include <string>

namespace NS
{
    namespace Anim
    {
        /// キーとキーの間の埋め方。3 次スプラインには対応しない
        enum class Interpolation
        {
            Linear,
            Step,
        };

        /// ボーンの TRS トラック。空のチャンネルは bindLocal を使う
        /// 骨を名前で持つので、どのモデルの骨格にも依らない。番号は再生する側が FindTrackBones で引く
        /// 時刻列の単位は秒
        struct BoneTrack
        {
            std::string boneName;

            std::vector<float> positionTimes;
            std::vector<Vector3> positionValues;
            Interpolation positionInterp = Interpolation::Linear;

            std::vector<float> rotationTimes;
            std::vector<Quaternion> rotationValues;
            Interpolation rotationInterp = Interpolation::Linear;

            std::vector<float> scaleTimes;
            std::vector<Vector3> scaleValues;
            Interpolation scaleInterp = Interpolation::Linear;
        };

        struct AnimClip
        {
            std::string name;
            float duration = 0.0f; // 尺 (秒)
            std::vector<BoneTrack> tracks;

            [[nodiscard]] bool IsValid() const noexcept { return duration > 0.0f && !tracks.empty(); }
        };

        /// 時刻 t 秒で評価する。Linear は線形補間、Step は左キーを保持する
        /// times は昇順。範囲外は端点、空なら fallback を返す
        [[nodiscard]] Vector3 SampleVec3(std::span<const float> times,
                                             std::span<const Vector3> values,
                                             Interpolation interp,
                                             float t,
                                             const Vector3& fallback) noexcept;

        /// 時刻 t 秒で評価する。Linear は最短経路の slerp、Step は左キーを保持する
        /// times は昇順で values と同数。範囲外は端点、空なら fallback を返す
        [[nodiscard]] Quaternion SampleQuat(std::span<const float> times,
                                                std::span<const Quaternion> values,
                                                Interpolation interp,
                                                float t,
                                                const Quaternion& fallback) noexcept;

        /// 時刻 t 秒の姿勢で outPose を上書きする。トラックのないボーンは bindLocal を使う
        /// trackBones は FindTrackBones が clip と skeleton から引いた骨の番号。-1 のトラックは使わない
        /// 出力の要素数は skeleton.BoneCount()
        void SampleClipPose(const AnimClip& clip,
                            std::span<const int> trackBones,
                            const Skeleton& skeleton,
                            float t,
                            std::vector<BonePose>& outPose);
    } // namespace Anim
} // namespace NS
