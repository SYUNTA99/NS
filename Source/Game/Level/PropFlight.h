#pragma once

#include "NSlib/Core/Math.h"

namespace GL
{
    namespace Level
    {
        //! 置物が今どう飛んでいるか。LaunchEffects の尾の向きと出す所を決める
        struct PropFlight
        {
            NS::Vector3 position{}; // 根の位置。世界座標
            NS::Vector3 velocity{}; // 自分で動かしている速度。単位は m/s
            bool arc = false;       // 飛んでいる中でも曲線の上にいるか
        };
    } // namespace Level
} // namespace GL
