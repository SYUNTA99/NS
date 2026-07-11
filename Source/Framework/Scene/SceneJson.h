#pragma once

/// @file SceneJson.h
/// @brief SceneData を正準 JSON へ往復させるシーン直列化
///
/// @details object の transform・コンポ一覧・material 表・環境を 1 つの JSON へ書く。 nlohmann 素の
/// `json` が object キーを辞書順・浮動小数を往復再現の最短表記で出力するため、 同一 SceneData の
/// 2 回保存は byte-identical になる。 読込は信頼できないローカルファイルを例外なく parse し、
/// version 一致の確認と要素数・file size の上限ガードで memory exhaustion を防ぐ
/// 依存: NS::Scene::SceneData、 nlohmann::json

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <filesystem>
#include <string>
#include <string_view>

namespace NS::Scene
{
    struct ComponentData;
    struct FieldValue;
    struct SceneData;

    /// SceneData を正準 JSON ファイルへ書く。 要素数 / 出力 size が上限超過なら false + `NS_LOG_ERROR`
    [[nodiscard]] bool SaveSceneToJsonFile(const SceneData& scene, const std::filesystem::path& path) noexcept;

    /// JSON ファイルを SceneData へ読む。 失敗時 false + `NS_LOG_ERROR`、 `outScene` は空 SceneData に reset される
    [[nodiscard]] bool LoadSceneFromJsonFile(SceneData& outScene, const std::filesystem::path& path) noexcept;

    /// SceneData を正準 JSON 文字列へ直列化する。 テスト・基準比較に使う
    [[nodiscard]] std::string SerializeSceneToJson(const SceneData& scene);

    /// JSON 文字列を SceneData へ復元する。 parse 失敗・version 不一致・上限超過で false、 `outScene` は空に reset
    /// される 読込成功時は object id の一意化と宙に浮いた ObjectRef の浄化まで済ませて返す
    [[nodiscard]] bool DeserializeSceneFromJson(SceneData& outScene, std::string_view jsonText);

    /// ComponentData の反射フィールドを {名前: 値} の JSON object へ写す
    /// 保存と実行時構築の反射適用が同じ変換を共有する唯一の経路
    [[nodiscard]] nlohmann::json ComponentFieldsToJson(const ComponentData& component);

    /// JSON 値 1 個を FieldValue へ推論復元する。 bool→bool / 小数→float / 整数→int / 配列3→Vector3 /
    /// 文字列→string / {"ref": id}→ObjectRef。 いずれにも合わなければ false で out は据え置き
    [[nodiscard]] bool JsonToFieldValue(const std::string& name, const nlohmann::json& value, FieldValue& out);

} // namespace NS::Scene
