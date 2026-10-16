#pragma once

#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Editor
    {
        struct AssetOption
        {
            // 保存する名前と、候補の表示名・場所を分ける。エフェクトはパスではなく読み取り側の名前を保存する
            std::string value;
            std::string label;
            std::string location;
        };

        //! contentRoot 配下の Assets と組み込みから候補を集める。保存用の道筋は contentRoot 相対
        //! アニメーションはファイル内の動きごと、エフェクトは Effects 相対の拡張子なしの名前を返す
        [[nodiscard]] std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot);
    }
}
