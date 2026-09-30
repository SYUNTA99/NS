#pragma once

// エディタで置ける物の一覧。ヒエラルキーの追加メニューとパレットが同じ一覧を読む
// 置ける物は TypeRegistry に NS_PLACEABLE で登録した Actor のクラスから組む。出荷ビルドには載らない

#include "Runtime/Object/ObjectJson.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS::Editor
{
    //! @brief エディタで置ける物 1 種類
    struct PlacementItem
    {
        std::string label;        //!< メニューとパレットに出す名前
        nlohmann::json prototype; //!< 置く時に写すひな形。class と値を持ち、id と位置は置く時に書く
        bool rotatable = false;   //!< パレットで 90 度ずつ回せるか。地形の部品の箱と坂だけ
    };

    //! @brief 置ける物の一覧
    //! @details 置ける Actor のクラスごとに 1 つ並べる。地形の部品だけは当たりの形 (立方体・球・坂) ごとに並べる
    [[nodiscard]] const std::vector<PlacementItem>& PlacementItems();

    //! label が一致する物。無ければ nullptr
    [[nodiscard]] const PlacementItem* FindPlacementItem(std::string_view label) noexcept;

    //! 地形の部品の立方体の名前。パレットの既定のブラシ
    inline constexpr std::string_view k_PartsCubeLabel = "地形の部品 (立方体)";
    //! 地形の部品の 45 度の坂の名前
    inline constexpr std::string_view k_PartsSlopeLabel = "地形の部品 (坂)";

    //! @brief メッシュ資産から置く地形の部品のひな形を作る
    //! @details 当たりは MeshCollider が描画と同じ三角形から作るので、描いた形と当たりがずれない
    //! @param[in] meshRef ContentRoot 相対のメッシュの参照
    [[nodiscard]] nlohmann::json MakeMeshPartsPrototype(std::string_view meshRef);
} // namespace NS::Editor
