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

        //! 素材の種類を拡張子で分ける。素材の窓・候補の一覧・素材の選択は、全部この判定を使う。
        //! 大文字と小文字は区別しない
        [[nodiscard]] bool IsMaterialAssetPath(std::string_view path);
        //! .gltf と .glb
        [[nodiscard]] bool IsModelAssetPath(std::string_view path);
        //! WICで表示する画像の拡張子か
        [[nodiscard]] bool IsTextureAssetPath(std::string_view path);
        //! .actor
        [[nodiscard]] bool IsActorFileAssetPath(std::string_view path);

        //! contentRoot 配下の Assets と組み込みから候補を集める。保存用の道筋は contentRoot 相対
        //! アニメーションはファイル内の動きごと、エフェクトは Effects 相対、音は Sounds 相対の拡張子なしの名前を返す
        [[nodiscard]] std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot);

        //! 六方向の PNG の存在、または単一の DDS キューブマップのヘッダーを確認する。
        //! 画像本体の読み込み結果はプレビューで確認する。
        [[nodiscard]] bool IsSkyboxAssetPath(std::string_view absolutePath);
        //! Assets 内の対応するフォルダと DDS を、プロジェクトからの相対パスで返す。
        [[nodiscard]] std::vector<AssetOption> CollectSkyboxOptions(std::string_view contentRoot);
    }
}
