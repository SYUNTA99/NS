#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/ObjectJson.h"

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
    //! @details {"version": 形式, "environment": {"skybox": パス}, "objects": [配置物の JSON...], "nextObjectId": 次の
    //! id} nextObjectId は単調増加で欠番は再利用しない。削除済み id が別物を指す事故を防ぐ
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

    //! @brief 重力の向きを長さ 1 に揃えて返す
    //! @return 有限でない成分を含むか長さがほぼ 0 なら (0, -1, 0)
    [[nodiscard]] NS::Vector3 NormalizeGravityDirection(const NS::Vector3& direction) noexcept;
    //! @brief environment.gravityDirection を長さ 1 に揃えて返す
    //! @return 無いか 3 つの数でなければ (0, -1, 0)
    [[nodiscard]] NS::Vector3 SceneJsonGravityDirection(const nlohmann::json& scene) noexcept;
    //! 長さ 1 に揃えた重力の向きを environment.gravityDirection へ書く。environment が無ければ作る
    void SetSceneJsonGravityDirection(nlohmann::json& scene, const NS::Vector3& direction);

    //! objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t k_NoObjectIndex = static_cast<std::size_t>(-1);

    //! 永続 id が id の配置物の添字。無ければ k_NoObjectIndex、k_NoObjectId は常に該当無し
    [[nodiscard]] std::size_t FindObjectIndexById(const nlohmann::json& scene, std::uint32_t id) noexcept;

    //! @details 未割当と重複には新 id を振り、nextObjectId を既存最大 id より先へ進める。重複は先勝ち
    void EnsureUniqueObjectIds(nlohmann::json& scene);

    //! ファイルの参照は名前で書くので、名前が 1 つに決まることを保存と読込が当てにする
    void EnsureUniqueObjectNames(nlohmann::json& scene);

    //! @brief 宙に浮いた参照を未設定へ戻し、直した件数を返す
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
