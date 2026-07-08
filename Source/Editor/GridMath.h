#pragma once

/// @file GridMath.h
/// @brief 編集モード用の grid 数学 helper。 EditorMode やギズモ、 各種 Command が再利用する
///
/// @details Component ではなく自由関数として NS::Editor 直下に置く。 出荷ビルドに載らない
/// editor 専用の計算で、 マウスの ray 化と grid snap、 回転値の quaternion 変換を担う


#include <cstdint>

namespace NS::Editor
{
    /// 1 grid サイズ。 世界座標で 1.0 m に対応する
    constexpr float kGridSize = 1.0f;

    /// マウスの screen 座標から world ray を生成する
    /// `viewProjection` は camera の VP matrix、 viewport は backbuffer サイズ
    [[nodiscard]] NS::Math::Ray ScreenToWorldRay(const NS::Math::Matrix& viewProjection,
                                                 NS::Math::Size2D viewport,
                                                 int mouseX,
                                                 int mouseY) noexcept;

    /// world 座標を最近接 cell 中心へ snap する
    [[nodiscard]] NS::Math::Vector3 SnapWorldPointToGrid(NS::Math::Vector3 worldPoint,
                                                         float gridSize = kGridSize) noexcept;

    /// AABB hit 結果から、 hit 面の法線方向に 1 grid offset した cell 中心を返す
    /// `hitNormal` は ±X / ±Y / ±Z のいずれか
    [[nodiscard]] NS::Math::Vector3 SnapHitToPlacementCell(NS::Math::Vector3 hitPoint,
                                                           NS::Math::Vector3 hitNormal,
                                                           float gridSize = kGridSize) noexcept;

    /// ray が y = 0 の ground plane と交わる点を grid snap して返す
    /// 上向き ray や後方ヒットなら false を返し `outCellCenter` は変更しない
    [[nodiscard]] bool TryGroundPlaneFallback(const NS::Math::Ray& ray,
                                              NS::Math::Vector3& outCellCenter,
                                              float gridSize = kGridSize) noexcept;

    /// 0..3 の回転値を Y 軸 90 度単位の quaternion に変換する
    [[nodiscard]] NS::Math::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept;
} // namespace NS::Editor
