#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/Skeleton.h"

#include <span>
#include <string>

namespace NS
{
    namespace Gfx
    {
        //! キーとキーの間の埋め方。3 次スプラインには対応しない
        enum class Interpolation
        {
            Linear,
            Step,
        };

        //! ボーンの TRS トラック。空のチャンネルは bindLocal を使う
        struct BoneTrack
        {
            int boneIndex = -1; //!< ボーン番号 (-1 は未割り当て)

            std::vector<float> positionTimes; //!< 位置キーの時刻列 (秒)
            std::vector<NS::Vector3> positionValues;
            Interpolation positionInterp = Interpolation::Linear;

            std::vector<float> rotationTimes; //!< 回転キーの時刻列 (秒)
            std::vector<NS::Quaternion> rotationValues;
            Interpolation rotationInterp = Interpolation::Linear;

            std::vector<float> scaleTimes; //!< スケールキーの時刻列 (秒)
            std::vector<NS::Vector3> scaleValues;
            Interpolation scaleInterp = Interpolation::Linear;
        };

        //! 描画用ポーズの評価に使うアニメーションクリップ
        struct AnimationClip
        {
            std::string name;
            float duration = 0.0f; // 尺 (秒)
            std::vector<BoneTrack> tracks;

            //! 尺が正でトラックが 1 本以上あれば true
            [[nodiscard]] bool IsValid() const noexcept { return duration > 0.0f && !tracks.empty(); }
        };

        //! 時刻 t 秒で評価する。Linear は線形補間、Step は左キーを保持する
        //! times は昇順。範囲外は端点、空なら fallback を返す
        [[nodiscard]] NS::Vector3 SampleVec3(std::span<const float> times,
                                             std::span<const NS::Vector3> values,
                                             Interpolation interp,
                                             float t,
                                             const NS::Vector3& fallback) noexcept;

        //! 時刻 t 秒で評価する。Linear は最短経路の slerp、Step は左キーを保持する
        //! times は昇順で values と同数。範囲外は端点、空なら fallback を返す
        [[nodiscard]] NS::Quaternion SampleQuat(std::span<const float> times,
                                                std::span<const NS::Quaternion> values,
                                                Interpolation interp,
                                                float t,
                                                const NS::Quaternion& fallback) noexcept;

        //! 時刻 t 秒の姿勢で outPose を上書きする。トラックのないボーンは bindLocal を使う
        //! 出力の要素数は skeleton.BoneCount()
        void SampleClipPose(const AnimationClip& clip,
                            const Skeleton& skeleton,
                            float t,
                            std::vector<BonePose>& outPose);
    } // namespace Gfx
} // namespace NS
