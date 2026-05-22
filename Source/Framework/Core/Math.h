#pragma once

/// @file math.h
/// @brief NS::Core 数学型エイリアスとヘルパー。
///
/// SimpleMath の型を using-alias で `NS::Core` に露出する単一ヘッダ。
/// 座標系は LH 一本。

#include <SimpleMath.h>

#include <DirectXCollision.h>

namespace NS::Core
{

    /// 2D ベクトル (float)
    using Vector2 = DirectX::SimpleMath::Vector2;

    /// 3D ベクトル (float)。位置・方向・スケール全般に使う
    using Vector3 = DirectX::SimpleMath::Vector3;

    /// 4D ベクトル (float)。同次座標・カラー前段に使う
    using Vector4 = DirectX::SimpleMath::Vector4;

    /// 4x4 行列。行列演算の本体型
    using Matrix = DirectX::SimpleMath::Matrix;

    /// Matrix の別名。CB レイアウト等で 4x4 と明示したい時に使う
    using Matrix4x4 = DirectX::SimpleMath::Matrix;

    /// クォータニオン。回転表現の本体型
    using Quaternion = DirectX::SimpleMath::Quaternion;

    /// 平面 (Ax + By + Cz + D = 0)
    using Plane = DirectX::SimpleMath::Plane;

    /// レイ (origin + direction)
    using Ray = DirectX::SimpleMath::Ray;

    /// ビューポート定義
    using Viewport = DirectX::SimpleMath::Viewport;

    /// RGBA カラー (float)
    using Color = DirectX::SimpleMath::Color;

    /// 軸並行バウンディングボックス。Center+Extents 表現
    using AABB = DirectX::BoundingBox;

    /// 円周率
    inline constexpr float kPi = 3.14159265358979323846f;

    /// 度 → ラジアン変換 (raw float)。 強い型を使う場合は ToRadians(Degrees) を優先。
    [[nodiscard]] constexpr float Deg2Rad(float deg) noexcept
    {
        return deg * (kPi / 180.0f);
    }

    /// ラジアン → 度変換 (raw float)。 強い型を使う場合は ToDegrees(Radians) を優先。
    [[nodiscard]] constexpr float Rad2Deg(float rad) noexcept
    {
        return rad * (180.0f / kPi);
    }

    /// 角度を弧度法 (radians) で持つ強い型。 暗黙変換禁止、 raw float の取り違え事故を防ぐ。
    /// 用途: Camera::SetFovY、 Transform 回転 API 等。 値は `value` メンバ経由で取り出す。
    struct Radians
    {
        float value;
    };

    /// 角度を度数法 (degrees) で持つ強い型。 ヒューマン向け数値リテラル用。
    /// 暗黙変換禁止、 ToRadians() 経由で明示変換が必要。
    struct Degrees
    {
        float value;
    };

    /// Degrees → Radians 明示変換。 sin/cos など radians 期待 API への入口。
    [[nodiscard]] constexpr Radians ToRadians(Degrees d) noexcept
    {
        return Radians{d.value * (kPi / 180.0f)};
    }

    /// Radians → Degrees 明示変換。 デバッグ表示・ ヒューマン UI 用。
    [[nodiscard]] constexpr Degrees ToDegrees(Radians r) noexcept
    {
        return Degrees{r.value * (180.0f / kPi)};
    }

    [[nodiscard]] constexpr bool operator==(Radians a, Radians b) noexcept
    {
        return a.value == b.value;
    }
    [[nodiscard]] constexpr bool operator==(Degrees a, Degrees b) noexcept
    {
        return a.value == b.value;
    }

    /// `value` を `[lo, hi]` の範囲にクランプする
    template <typename T> [[nodiscard]] constexpr T Clamp(T value, T lo, T hi) noexcept
    {
        return (value < lo) ? lo : (value > hi) ? hi : value;
    }

    /// 線形補間。t=0 で a、t=1 で b。範囲外は外挿
    [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

} // namespace NS::Core
