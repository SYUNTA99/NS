#pragma once

/// @file LevelJson.h
/// @brief LevelData を正準 JSON へ往復させるレベル直列化
///
/// @details object ヘッダ (transform / kind / collider) + コンポ一覧 + material 表 + camera + meta を
/// 1 つの JSON へ書く。 nlohmann 素の `json` が object キーを辞書順・ float を shortest round-trip で
/// 出力するため、 同一 LevelData の 2 回保存は byte-identical になる。 読込は信頼できないローカルファイルを
/// 例外なく parse し、 要素数・ file size の上限ガードで memory exhaustion を防ぐ
/// 依存: NS::Game::Level::LevelData、 nlohmann::json (実装内)

#include <filesystem>
#include <string>
#include <string_view>

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Game::Level
{
    struct LevelData;
    struct ComponentData;

    /// LevelData を正準 JSON ファイルへ書く。 要素数 / 出力 size が上限超過なら false + `NS_LOG_ERROR`
    [[nodiscard]] bool SaveLevelToJsonFile(const LevelData& level, const std::filesystem::path& path) noexcept;

    /// JSON ファイルを LevelData へ読む。 失敗時 false + `NS_LOG_ERROR`、 `outLevel` は空 LevelData に reset される
    [[nodiscard]] bool LoadLevelFromJsonFile(LevelData& outLevel, const std::filesystem::path& path) noexcept;

    /// LevelData を正準 JSON 文字列へ直列化する (テスト・ golden 比較用)
    [[nodiscard]] std::string SerializeLevelToJson(const LevelData& level);

    /// JSON 文字列を LevelData へ復元する。 parse 失敗・上限超過で false (`outLevel` は空に reset)
    [[nodiscard]] bool DeserializeLevelFromJson(LevelData& outLevel, std::string_view jsonText);

    /// ComponentData の反射フィールドを {名前: 値} の JSON object へ写す
    /// 保存と BuildPlacedObject の ApplyJsonFields 入力が同じ変換を共有する唯一の経路
    [[nodiscard]] nlohmann::json ComponentFieldsToJson(const ComponentData& component);
} // namespace NS::Game::Level
