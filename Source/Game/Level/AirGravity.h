#pragma once

#include <cmath>

namespace GL
{
    namespace Level
    {
        //! 空中の重力の組。上りと下りで強さを分け、頂点付近を弱める
        //! 自機の移動・突進の予測・飛ばされて曲がった後の置物が、同じ ChooseGravity で重力を選ぶ
        struct AirGravity
        {
            float rise = 0.0f;      // 上向きの間の重力 (m/s²)。下向きが負
            float fall = 0.0f;      // 上向きでない間の重力 (m/s²)。下向きが負
            float apexSpeed = 0.0f; // 縦の速さの大きさがこれ未満の間を頂点付近とする (m/s)
            float apexScale = 0.0f; // 頂点付近で重力へ掛ける倍率
        };

        //! 上昇中は rise、それ以外は fall を選ぶ。頂点付近は apexScale を掛ける。境目の速さは頂点付近に入れない
        //! verticalVelocity は m/s、戻り値は m/s²。上が正
        [[nodiscard]] inline float ChooseGravity(const AirGravity& gravity, float verticalVelocity) noexcept
        {
            float strength = gravity.fall;
            if (verticalVelocity > 0.0f)
            {
                strength = gravity.rise;
            }
            if (std::abs(verticalVelocity) < gravity.apexSpeed)
            {
                strength *= gravity.apexScale;
            }
            return strength;
        }
    } // namespace Level
} // namespace GL
