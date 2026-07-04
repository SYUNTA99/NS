#pragma once

/// @file ThemeData.h
/// @brief 1 テーマの全データ。 ThemeRegistry が 5 件を保持し `.theme` ファイル読込で上書きする
///
/// @details 物理は全テーマ共通なので skybox と lighting の視覚フィールドのみを持つ純データ表現

#include <cstdint>
#include <filesystem>
#include <string>

#include "Framework/Math/Math.h"

namespace NS::Game::Theme
{

    struct ThemeData
    {
        /// ImGui パレットや debug overlay で使うテーマ表示名。 ファイル読込で差し替わるため所有する
        std::string displayName{};
        /// block texture 配列の先頭 slice index。 テーマごとに 8 slice 帯を確保する想定
        std::uint16_t blockTextureArrayBaseSlice = 0;
        /// skybox cubemap のディレクトリ or .dds パス。 Skybox::LoadCubemap に渡す
        std::filesystem::path skyboxCubemapPath{};
        /// directional sun の向き。 正規化前でよく、 シェーダ側で normalize する
        NS::Math::Vector3 lightDirection{-0.3f, -1.0f, -0.2f};
        /// directional sun の色。 HDR 込みで 1.3 等を許容する
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
        /// 環境光 ambient の色。 N.L = 0 の影側ベース色になる
        NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
    };

} // namespace NS::Game::Theme
