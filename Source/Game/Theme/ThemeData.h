#pragma once

/// @file ThemeData.h
/// @brief 1 テーマの全データ。 POD struct、 ThemeRegistry が 5 件を read-only で保持する
///
/// @details ECS 禁止の locked constraint に従い「振る舞いが同じならクラス化しない」 純データ表現
/// Texture2DArray slice は今後 blockTextureArrayBaseSlice を本格活用するため、
/// ここではフィールドのみ用意する。 物理は全テーマ共通なので視覚フィールド (skybox + lighting)
/// のみを持たせる。 Game partition convention により namespace 無し

#include <cstdint>
#include <filesystem>

#include "Framework/Math/Math.h"

struct ThemeData
{
    /// テーマ表示名 (ImGui パレット / debug overlay 用、 文字列リテラルへの非所有 ptr)
    const char* displayName = "";
    /// block texture 配列の先頭 slice index (テーマごとに 8 slice 帯を確保する想定)
    std::uint16_t blockTextureArrayBaseSlice = 0;
    /// skybox cubemap のディレクトリ or .dds パス。 Skybox::LoadCubemap に渡す
    std::filesystem::path skyboxCubemapPath{};
    /// directional sun の向き (正規化前で OK、 シェーダ側で normalize する)
    NS::Math::Vector3 lightDirection{-0.3f, -1.0f, -0.2f};
    /// directional sun の色 (HDR 込み、 1.3 等を許容)
    NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
    /// ambient (環境光) の色。 N.L = 0 の影側ベース色になる
    NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
};
