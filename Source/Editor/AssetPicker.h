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
            std::string value; // 保存値（エフェクトはパスではなく読み取り側の名前）
            std::string label;
            std::string location;
        };

        [[nodiscard]] bool IsMaterialAssetPath(std::string_view path);
        [[nodiscard]] bool IsModelAssetPath(std::string_view path);
        [[nodiscard]] bool IsTextureAssetPath(std::string_view path);
        [[nodiscard]] bool IsActorFileAssetPath(std::string_view path);

        /// contentRoot 配下の Assets と組み込みから候補を集める（保存値は contentRoot 相対パス、アニメーションは #クリップ名 付き、エフェクトは Effects 相対の拡張子なし名）
        [[nodiscard]] std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot);

        /// 6 方向の PNG を持つフォルダ、または単一の DDS キューブマップかをヘッダーまで確認して判定する
        [[nodiscard]] bool IsSkyboxAssetPath(std::string_view absolutePath);
        /// Assets 内のスカイボックス候補（フォルダおよび DDS）を contentRoot 相対パスで返す
        [[nodiscard]] std::vector<AssetOption> CollectSkyboxOptions(std::string_view contentRoot);
    }
}
