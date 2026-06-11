#pragma once

/// @file RenderSettings.h
/// @brief 描画のプロジェクト既定値 POD とシーン上書き POD、 両者を merge する Resolve

#include "Framework/Math/Math.h"

#include <optional>

namespace NS::Graphics
{
    /// 描画の既定値を完全な値で保持する POD
    /// プロジェクトで 1 個だけ RendererDesc 経由で Renderer が保持し、 シーンは override だけ書く
    struct RenderSettings
    {
        /// backbuffer クリア色 (RGBA)
        NS::Math::Color clearColor{0.10f, 0.10f, 0.15f, 1.0f};
        /// directional sun の向き (正規化前で OK、 シェーダ側で normalize)
        NS::Math::Vector3 lightDir{-0.3f, -1.0f, -0.2f};
        /// directional sun の色 (HDR 込み)
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
        /// 環境光の色 (N.L = 0 の影側ベース色)
        NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
    };

    /// 指定フィールドだけを上書きする POD
    /// 未指定 (nullopt) のフィールドは Resolve で既定値が残る
    struct RenderSettingsOverride
    {
        std::optional<NS::Math::Color> clearColor;
        std::optional<NS::Math::Vector3> lightDir;
        std::optional<NS::Math::Vector3> lightColor;
        std::optional<NS::Math::Vector3> ambientColor;
    };

    /// defaults を基に over の has_value フィールドだけ差し替えた新しい設定を返す純粋関数
    /// @param defaults プロジェクト既定値
    /// @param over シーン単位の上書き
    /// @retresult 解決済み設定 (引数は変更しない)
    [[nodiscard]] RenderSettings Resolve(const RenderSettings& defaults, const RenderSettingsOverride& over) noexcept;
} // namespace NS::Graphics
