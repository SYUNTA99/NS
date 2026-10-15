#pragma once

// エディタで置ける物の一覧。ヒエラルキーの追加メニューとパレットが同じ一覧を読む
// 置ける物は TypeRegistry に NS_PLACEABLE で登録した SceneActor のクラスから組む。出荷ビルドには載らない

#include "NSlib/Object/ObjectJson.h"

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

    //! 登録済みの配置可能な SceneActor を並べる。地形の部品はメッシュの形ごとに分ける
    [[nodiscard]] const std::vector<PlacementItem>& PlacementItems();

    //! label が一致する物。無ければ nullptr
    [[nodiscard]] const PlacementItem* FindPlacementItem(std::string_view label) noexcept;

    //! 地形の部品の立方体の名前。パレットの既定のブラシ
    inline constexpr std::string_view k_PartsCubeLabel = "地形の部品 (立方体)";
    //! 地形の部品の 45 度の坂の名前
    inline constexpr std::string_view k_PartsSlopeLabel = "地形の部品 (坂)";

    //! ContentRoot 相対の meshRef から地形のひな形を作る。衝突は描画と同じ三角形を使う
    [[nodiscard]] nlohmann::json MakeMeshPartsPrototype(std::string_view meshRef);

    //! ひな形の見た目が組み込みの坂なら、その角度 (度)。坂でなければ負の値
    [[nodiscard]] float PartsSlopeAngleDegrees(const nlohmann::json& prototype) noexcept;
} // namespace NS::Editor
