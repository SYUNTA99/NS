#pragma once

#include "Runtime/Object/ObjectJson.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// シーン 1 つの JSON 文書と、その読み書き・読込時の整え
// 実体は Scene と配置物だけで、実体でないシーンの姿 (ファイル・プレイ開始時の凍結) はこの JSON で持つ
// メモリ上とファイル上の違いは参照の書き方だけで、メモリ上は id、ファイル上は名前
// nlohmann の既定辞書順キー出力で、同じ文書の 2 回保存は byte 一致になる

namespace NS::Obj
{
    //! @brief 空のシーン文書を作る
    //! @details {"version": 形式, "environment": {"skybox": パス}, "objects": [配置物の JSON...], "nextObjectId": 次の id}
    //! nextObjectId は単調増加で欠番は再利用しない。削除済み id が別物を指す事故を防ぐ
    [[nodiscard]] nlohmann::json MakeSceneJson();

    //! 配置物の JSON の配列。無いか壊れていれば空の配列
    [[nodiscard]] const nlohmann::json& SceneJsonObjects(const nlohmann::json& scene) noexcept;
    //! 配置物の JSON の配列を返し、無ければ作る
    [[nodiscard]] nlohmann::json& SceneJsonObjects(nlohmann::json& scene);

    //! 次に割り当てる永続 id。無ければ 1
    [[nodiscard]] std::uint32_t SceneJsonNextObjectId(const nlohmann::json& scene) noexcept;
    void SetSceneJsonNextObjectId(nlohmann::json& scene, std::uint32_t nextObjectId);

    //! skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。空文字なら skybox を描かない
    [[nodiscard]] std::string_view SceneJsonSkybox(const nlohmann::json& scene) noexcept;
    void SetSceneJsonSkybox(nlohmann::json& scene, std::string_view path);

    //! objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t k_NoObjectIndex = static_cast<std::size_t>(-1);

    //! 永続 id が id の配置物の添字。無ければ k_NoObjectIndex、k_NoObjectId は常に該当無し
    [[nodiscard]] std::size_t FindObjectIndexById(const nlohmann::json& scene, std::uint32_t id) noexcept;

    //! @brief 全配置物と全 component の永続 id を「非 0 かつ一意」へ整える
    //! @details 未割当と重複には新 id を振り、nextObjectId を既存最大 id より先へ進める。重複は先勝ち
    //! 番号の空間は配置物と component で共通なので、id 1 個で世界の誰か 1 人が決まる
    //! 続けて EnsureUniqueObjectNames で配置物と component の名前も一意にする
    void EnsureUniqueObjectIds(nlohmann::json& scene);

    //! @brief 全配置物に一意な名前を付け、各配置物の component にも配置物の中で一意な名前を付ける
    //! @details 先に付いていた名前を保ち、空と重複にだけ新しい名前を振る。名前の無い component は型名から付ける
    //! ファイルの参照は名前で書くので、名前が 1 つに決まることを保存と読込が当てにする
    void EnsureUniqueObjectNames(nlohmann::json& scene);

    //! @brief 宙に浮いた参照を未設定へ戻し、直した件数を返す
    //! @details ObjectRef は持ち主が居なければ 0 へ、ComponentRef は持ち主か Component が居なければ両方 0 へ戻す
    [[nodiscard]] std::size_t PruneDanglingObjectRefs(nlohmann::json& scene);

    //! 辿れない親を root の 0 へ戻し、直した件数を返す。自分自身・不在の親・循環が対象
    [[nodiscard]] std::size_t PruneInvalidParents(nlohmann::json& scene);

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
} // namespace NS::Obj
