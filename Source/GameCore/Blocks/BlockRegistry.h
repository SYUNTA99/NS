#pragma once

/// @file BlockRegistry.h
/// @brief grid 配置物の 90° 回転ヘルパ
///
/// @details grid 配置物の向きは 0..3 の 4 段階で Y 軸 90° 刻み
/// その step を yaw ラジアンへ変換する関数と段階数だけを提供する

#include <cstdint>

namespace NS::Game::Blocks
{
    /// grid 配置物の向きの分解能。 0..3 を Y 軸 90° 刻みの 4 方向へ割り当てる
    inline constexpr std::uint16_t kBlockRotationSteps = 4;

    /// 0..3 の回転値を Y 軸 yaw ラジアンに変換する
    /// 描画と当たり判定が同じ向きになるよう全経路でこれを使う
    [[nodiscard]] float BlockRotationToYaw(std::uint8_t rotation) noexcept;
} // namespace NS::Game::Blocks
