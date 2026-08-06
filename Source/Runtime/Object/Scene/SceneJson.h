#pragma once

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <filesystem>
#include <string>
#include <string_view>

// SceneData を正準 JSON へ往復させるシーン直列化
// nlohmann の既定辞書順キー出力で、同一 SceneData の 2 回保存は byte 一致になる

namespace NS::Object
{
    struct SceneData;

    /// SceneData を正準 JSON ファイルへ書く。 要素数 / 出力 size が上限超過なら false + `NS_LOG_ERROR`
    [[nodiscard]] bool SaveSceneToJsonFile(const SceneData& scene, const std::filesystem::path& path) noexcept;

    /// JSON ファイルを SceneData へ読む。 失敗時 false + `NS_LOG_ERROR`、 `outScene` は空 SceneData に reset される
    [[nodiscard]] bool LoadSceneFromJsonFile(SceneData& outScene, const std::filesystem::path& path) noexcept;

    /// SceneData を正準 JSON 文字列へ直列化する。 テスト・基準比較に使う
    [[nodiscard]] std::string SerializeSceneToJson(const SceneData& scene);

    /// JSON 文字列を SceneData へ復元する。 parse 失敗・version 不一致・上限超過で false、 `outScene` は空に reset
    /// される 読込成功時は object id の一意化と宙に浮いた ObjectRef の除去まで済ませて返す
    [[nodiscard]] bool DeserializeSceneFromJson(SceneData& outScene, std::string_view jsonText);

} // namespace NS::Object
