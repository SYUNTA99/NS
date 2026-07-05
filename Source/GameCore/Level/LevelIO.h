#pragma once

/// @file LevelIO.h
/// @brief レベル save / load の呼び出し窓口。 実体は LevelJson の正準 JSON 直列化へ委譲する
///
/// @details 呼出側の editor / play scene が保存形式を知らずに済むよう、 形式非依存の
/// `SaveLevelToFile` / `LoadLevelFromFile` だけを公開する

#include <filesystem>

namespace NS::Game::Level
{

    struct LevelData;

    /// 1 関数で LevelData を保存する。 失敗時は false + `NS_LOG_ERROR`
    [[nodiscard]] bool SaveLevelToFile(const LevelData& level, const std::filesystem::path& path) noexcept;

    /// 1 関数で LevelData を読み込む。 失敗時は false + `NS_LOG_ERROR`、
    /// `outLevel` は default-constructed の空 LevelData に reset される
    [[nodiscard]] bool LoadLevelFromFile(LevelData& outLevel, const std::filesystem::path& path) noexcept;

} // namespace NS::Game::Level
