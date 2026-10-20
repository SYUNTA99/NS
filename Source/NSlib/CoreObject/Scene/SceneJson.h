#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ObjectJson.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

// 保存・プレイ開始時の控えに使うシーン文書。参照はメモリ上で id、ファイル上で名前を使う
// キーを辞書順で書くため、同じ文書を保存した結果はバイト単位で一致する

namespace NS
{
    //! 既定の空文書を作る。nextObjectId は単調増加し、削除済み id を再利用しない
    [[nodiscard]] nlohmann::json MakeSceneJson();

    //! 配置物の JSON の配列。無いか壊れていれば空の配列
    [[nodiscard]] const nlohmann::json& SceneJsonObjects(const nlohmann::json& scene) noexcept;
    //! 配置物の JSON の配列を返し、無ければ作る
    [[nodiscard]] nlohmann::json& SceneJsonObjects(nlohmann::json& scene);

    //! 次に割り当てる永続 id。無ければ 1
    [[nodiscard]] uint32 SceneJsonNextObjectId(const nlohmann::json& scene) noexcept;
    void SetSceneJsonNextObjectId(nlohmann::json& scene, uint32 nextObjectId);

    //! @brief 次に置く配置物の id。nextObjectId と、配置物の最大の id の次の大きい方
    //! @details 手編集で nextObjectId が配置物の id より小さいファイルもある。1 より小さくならない
    [[nodiscard]] uint32 NextFreeObjectId(const nlohmann::json& scene) noexcept;

    //! シーンの environment の欄
    struct SceneEnvironment
    {
        //! skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。空文字なら skybox を描かない
        std::string skybox;
    };

    //! environment を読む。無い欄と壊れた欄は SceneEnvironment の既定
    [[nodiscard]] SceneEnvironment ReadSceneEnvironment(const nlohmann::json& scene);
    //! environment を丸ごと書き換える
    void WriteSceneEnvironment(nlohmann::json& scene, const SceneEnvironment& environment);
    //! ReadSceneEnvironment の skybox
    [[nodiscard]] std::string SceneJsonSkybox(const nlohmann::json& scene);

    //! objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t k_NoObjectIndex = static_cast<std::size_t>(-1);

    //! 永続 id が id の配置物の添字。無ければ k_NoObjectIndex、k_NoObjectId は常に該当無し
    [[nodiscard]] std::size_t FindObjectIndexById(const nlohmann::json& scene, uint32 id) noexcept;

    //! @details 未割当と重複には新 id を振り、nextObjectId を既存最大 id より先へ進める。重複は先勝ち
    void EnsureUniqueObjectIds(nlohmann::json& scene);

    //! 未使用の名前を返す。空なら Object、重複したら _1、_2 と番号を付ける
    [[nodiscard]] std::string MakeUniqueActorName(std::string_view base, const std::unordered_set<std::string>& used);

    //! シーン文書をファイルの JSON 文字列へ直列化する。参照は名前へ直して書く
    [[nodiscard]] std::string SerializeSceneToJson(const nlohmann::json& scene);

    //! @brief ファイルの JSON 文字列をシーン文書へ復元する
    //! @details parse 失敗・version 不一致・上限超過で false、outScene は空の文書へ戻す
    //! 成功時は id と名前の一意化、名前で書かれた参照の id への変換、宙に浮いた参照と辿れない親の除去まで済ませる
    [[nodiscard]] bool DeserializeSceneFromJson(nlohmann::json& outScene, std::string_view jsonText);

    //! シーン文書をファイルへ書く。要素数 / 出力 size が上限超過なら false + NS_LOG_ERROR
    [[nodiscard]] bool SaveSceneToJsonFile(const nlohmann::json& scene, std::string_view path) noexcept;

    //! ファイルをシーン文書へ読む。失敗時 false + NS_LOG_ERROR、outScene は空の文書へ戻す
    [[nodiscard]] bool LoadSceneFromJsonFile(nlohmann::json& outScene, std::string_view path) noexcept;
} // namespace NS
