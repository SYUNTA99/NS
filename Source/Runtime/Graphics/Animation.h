#pragma once

#include "Runtime/Graphics/Skeleton.h"
#include "Runtime/Math/Math.h"

#include <span>
#include <string>

namespace NS::Graphics
{
    /// 3Dモデルフォーマットの線形とステップに対応（3次スプラインは非対応）。
    enum class Interpolation
    {
        Linear,
        Step,
    };

    /// @brief 1 ボーンの TRS キーフレームトラック
    /// @details 各チャンネルは独立した時刻列・補間方式を持つ
    /// 空のチャンネルはそのボーンの bindLocal 値を据え置く
    struct BoneTrack
    {
        int boneIndex = -1; ///< ボーン番号（-1は未割り当て）

        std::vector<float> positionTimes;                     ///< 位置キーの時刻列 (秒)
        std::vector<NS::Math::Vector3> positionValues;        ///< 位置キーの値
        Interpolation positionInterp = Interpolation::Linear; ///< 位置の補間方式

        std::vector<float> rotationTimes;                     ///< 回転キーの時刻列 (秒)
        std::vector<NS::Math::Quaternion> rotationValues;     ///< 回転キーの値
        Interpolation rotationInterp = Interpolation::Linear; ///< 回転の補間方式

        std::vector<float> scaleTimes;                     ///< スケールキーの時刻列 (秒)
        std::vector<NS::Math::Vector3> scaleValues;        ///< スケールキーの値
        Interpolation scaleInterp = Interpolation::Linear; ///< スケールの補間方式
    };

    /// 評価結果は描画用ポーズの計算に使用される
    struct AnimationClip
    {
        std::string name;              // クリップ名
        float duration = 0.0f;         // 尺 (秒)
        std::vector<BoneTrack> tracks; // 動くボーンのトラック群

        /// 尺が正でトラックが 1 本以上あれば true
        [[nodiscard]] bool IsValid() const noexcept { return duration > 0.0f && !tracks.empty(); }
    };

    /// Vector3 トラックを時刻 t（秒）で評価する
    /// @param times   各キーの時刻（秒、昇順）
    /// @param values  各キーの値
    /// @param interp  補間方法。Linear=線形補間、Step=左キー保持
    /// @param t       評価する時刻（秒）
    /// @param fallback times が空のとき返す値
    /// @return 補間された値。範囲外は端点クランプ
    [[nodiscard]] NS::Math::Vector3 SampleVec3(std::span<const float> times,
                                               std::span<const NS::Math::Vector3> values,
                                               Interpolation interp,
                                               float t,
                                               const NS::Math::Vector3& fallback) noexcept;

    /// @brief Quaternion トラックを時刻 t で評価する
    /// @details Linear=最短経路 slerp、Step=左キー保持、範囲外=端点クランプ。
    /// @param times    各キーの時刻（秒）。昇順であること
    /// @param values   各キーの回転値。times と同数
    /// @param interp   補間方法（Linear / Step）
    /// @param t        評価する時刻（秒）
    /// @param fallback times が空のとき返す値
    /// @return 時刻 t における回転。範囲外は端点にクランプ
    [[nodiscard]] NS::Math::Quaternion SampleQuat(std::span<const float> times,
                                                  std::span<const NS::Math::Quaternion> values,
                                                  Interpolation interp,
                                                  float t,
                                                  const NS::Math::Quaternion& fallback) noexcept;

    /// @brief clip を時刻 t で評価し pose を埋める
    /// @details トラックを持たないボーンは skeleton の bindLocal を使う。
    /// @param clip     評価するアニメーションクリップ
    /// @param skeleton ボーン構成。出力サイズの基準になる
    /// @param t        評価する時刻（秒）
    /// @param outPose  結果の書き込み先。呼び出し後に内容が上書きされる
    /// @post outPose.size() == skeleton.BoneCount()
    void SampleClipPose(const AnimationClip& clip, const Skeleton& skeleton, float t, std::vector<BonePose>& outPose);

} // namespace NS::Graphics
