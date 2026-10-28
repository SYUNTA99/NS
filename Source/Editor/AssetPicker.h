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
            std::string value; // 保存値（エフェクトと演出セットはパスではなく識別子）
            std::string label;
            std::string location;
        };

        [[nodiscard]] bool IsMaterialAssetPath(std::string_view path);
        [[nodiscard]] bool IsModelAssetPath(std::string_view path);
        [[nodiscard]] bool IsTextureAssetPath(std::string_view path);
        [[nodiscard]] bool IsActorFileAssetPath(std::string_view path);
        //! エフェクトの素材の定義 (.effect) か演出セット (.effectset) か。拡張子の大文字小文字は問わない
        [[nodiscard]] bool IsEffectAssetPath(std::string_view path);
        //! Effekseer のファイル (.efkefc) か。拡張子の大文字小文字は問わない
        [[nodiscard]] bool IsEffekseerAssetPath(std::string_view path);

        //! contentRoot 配下の Assets と組み込みから候補を集める。保存用の道筋は contentRoot 相対
        //! アニメーションはファイル内の動きごと、音は Sounds 相対の拡張子なしの名前を返す
        //! エフェクトと演出セットは、Assets/Effects の定義の識別子を返し、表示名で並べる
        [[nodiscard]] std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot);

        //! 6 方向の PNG を持つフォルダ、または単一の DDS キューブマップかをヘッダーまで確認して判定する
        [[nodiscard]] bool IsSkyboxAssetPath(std::string_view absolutePath);
        //! Assets 内のスカイボックス候補（フォルダおよび DDS）を contentRoot 相対パスで返す
        [[nodiscard]] std::vector<AssetOption> CollectSkyboxOptions(std::string_view contentRoot);
    }
}
