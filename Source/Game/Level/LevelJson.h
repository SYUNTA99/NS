#pragma once

/// @file LevelJson.h
/// @brief NS::Scene::SceneData を正準 JSON へ往復させるレベル直列化
///
/// @details object ヘッダの transform / collider、 コンポ一覧、 material 表、 camera、 meta を
/// 1 つの JSON へ書く。 nlohmann 素の `json` が object キーを辞書順・ float を shortest round-trip で
/// 出力するため、 同一 NS::Scene::SceneData の 2 回保存は byte-identical になる。 読込は信頼できないローカルファイルを
/// 例外なく parse し、 要素数・ file size の上限ガードで memory exhaustion を防ぐ
/// 依存: NS::Scene::SceneData、 nlohmann::json は実装内でのみ使う

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Scene
{
    struct ComponentData;
    struct FieldValue;
    struct SceneData;
} // namespace NS::Scene

namespace NS::Game::Level
{
    // 読込時移行の報告。 定義は呼び出し窓口の LevelIO.h (ここではポインタ受け渡しのみ)
    struct LevelLoadReport;

    /// NS::Scene::SceneData を正準 JSON ファイルへ書く。 要素数 / 出力 size が上限超過なら false + `NS_LOG_ERROR`
    [[nodiscard]] bool SaveLevelToJsonFile(const NS::Scene::SceneData& level, const std::filesystem::path& path) noexcept;

    /// JSON ファイルを NS::Scene::SceneData へ読む。 失敗時 false + `NS_LOG_ERROR`、 `outLevel` は空 NS::Scene::SceneData に reset される
    /// outReport 非 null なら読込時移行の報告を書き込む
    [[nodiscard]] bool LoadLevelFromJsonFile(NS::Scene::SceneData& outLevel,
                                             const std::filesystem::path& path,
                                             LevelLoadReport* outReport = nullptr) noexcept;

    /// NS::Scene::SceneData を正準 JSON 文字列へ直列化する。 テスト・ golden 比較に使う
    [[nodiscard]] std::string SerializeLevelToJson(const NS::Scene::SceneData& level);

    /// JSON 文字列を NS::Scene::SceneData へ復元する。 parse 失敗・上限超過で false、 `outLevel` は空に reset される
    /// outReport 非 null なら読込時移行の報告を書き込む
    [[nodiscard]] bool DeserializeLevelFromJson(NS::Scene::SceneData& outLevel,
                                                std::string_view jsonText,
                                                LevelLoadReport* outReport = nullptr);

    /// NS::Scene::ComponentData の反射フィールドを {名前: 値} の JSON object へ写す
    /// 保存と BuildPlacedObject の ApplyJsonFields 入力が同じ変換を共有する唯一の経路
    [[nodiscard]] nlohmann::json ComponentFieldsToJson(const NS::Scene::ComponentData& component);

    /// JSON 値 1 個を NS::Scene::FieldValue へ推論復元する。 bool→bool / 小数→float / 整数→int / 配列3→Vector3 /
    /// 文字列→string / {"ref": id}→ObjectRef。 いずれにも合わなければ false で out は据え置き
    /// レベル読込と PlayerTuning テンプレートの取込が同じ変換を共有する
    [[nodiscard]] bool JsonToFieldValue(const std::string& name, const nlohmann::json& value, NS::Scene::FieldValue& out);
} // namespace NS::Game::Level
