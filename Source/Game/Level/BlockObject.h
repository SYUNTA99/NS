#pragma once

// 地形の単位になる 1m 立方の固形ブロック
// 見た目は cube mesh、当たりは Box collider で、レベルの床も壁も足場もこれを並べて作る

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <optional>
#include <string_view>

namespace NS::Object
{
    class GameObject;
} // namespace NS::Object

namespace NS::Game::Level
{
    /// ブロック 1 個 (1m 立方) の半サイズ。cube 描画と Box 当たりが共有する
    inline constexpr NS::Math::Vector3 k_CellHalfExtents{0.5f, 0.5f, 0.5f};

    /// テクスチャ未解決時のフォールバック用基準色
    inline constexpr NS::Math::Vector3 k_SolidBaseColor{0.70f, 0.70f, 0.75f};

    /// MeshRendererComponent の component entry を作る。Mesh / Material / Base Color を書き込む
    [[nodiscard]] nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                                       std::string_view materialName,
                                                       const NS::Math::Vector3& baseColor);

    /// 基本キューブ（cube 描画 + Box 当たり）の構成を生成する
    [[nodiscard]] nlohmann::json MakeCellCubeComponents();

    /// cell の x, y, z に既定 solid の ObjectData を作る
    [[nodiscard]] NS::Object::ObjectData MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z);

    /// 純粋な solid ブロックか。当たり Box を持ち、斜面・ダメージ・拾得・即死の性格が付いていない
    /// live とエディタの「solid とは何か」をこの 1 本に揃える
    [[nodiscard]] bool IsSolidBoxRule(
        bool hasBox, bool hasSlope, bool hasHazard, bool hasGoal, bool hasKillZone) noexcept;

    /// 固形ブロックのワールド OBB。solid でなければ nullopt
    [[nodiscard]] std::optional<NS::Math::OBB> SolidBoxWorldOBB(NS::Object::GameObject& obj) noexcept;
} // namespace NS::Game::Level
