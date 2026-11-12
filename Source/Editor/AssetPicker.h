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
            std::string value; // 保存値（ContentRoot からの相対パス、または組み込み名）
            std::string label;
            std::string location;
        };

        [[nodiscard]] bool IsMaterialAssetPath(std::string_view path);
        [[nodiscard]] bool IsModelAssetPath(std::string_view path);
        [[nodiscard]] bool IsTextureAssetPath(std::string_view path);
        [[nodiscard]] bool IsActorFileAssetPath(std::string_view path);
        //! Effekseer のファイル (.efkefc) か。拡張子の大文字小文字は問わない
        [[nodiscard]] bool IsEffekseerAssetPath(std::string_view path);

        //! contentRoot 配下の Assets と組み込みから候補を集める。保存用の道筋は contentRoot 相対
        //! アニメーションはファイル内の動きごと、音は Assets/Sounds の .wav、エフェクトは Assets/Effects の .efkefc を返す
        [[nodiscard]] std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot);

        //! 6 方向の PNG を持つフォルダ、または単一の DDS キューブマップかをヘッダーまで確認して判定する
        [[nodiscard]] bool IsSkyboxAssetPath(std::string_view absolutePath);
        //! Assets 内のスカイボックス候補（フォルダおよび DDS）を contentRoot 相対パスで返す
        [[nodiscard]] std::vector<AssetOption> CollectSkyboxOptions(std::string_view contentRoot);
    }
}
