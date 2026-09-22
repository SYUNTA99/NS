#pragma once

// 地形の単位になる 1m 立方の固形ブロック
// 見た目は cube mesh、当たりは Box collider で、レベルの床も壁も足場もこれを並べて作る

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <string_view>

namespace NS::Game::Level
{
    //! ブロック 1 個 (1m 立方) の半サイズ。cube 描画と Box 当たりが共有する
    inline constexpr NS::Core::Vector3 k_CellHalfExtents{0.5f, 0.5f, 0.5f};

    //! テクスチャ未解決時のフォールバック用基準色
    inline constexpr NS::Core::Vector3 k_SolidBaseColor{0.70f, 0.70f, 0.75f};

    //! MeshRendererComponent の component entry を作る。Mesh / Material / Base Color を書き込む
    [[nodiscard]] nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                                       std::string_view materialName,
                                                       const NS::Core::Vector3& baseColor);

    //! cube 描画と Box 当たりを積んだ、基本キューブの構成を作る
    [[nodiscard]] nlohmann::json MakeCellCubeComponents();

    //! cell の x, y, z に既定 solid の ObjectData を作る
    [[nodiscard]] NS::Obj::ObjectData MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z);
} // namespace NS::Game::Level
