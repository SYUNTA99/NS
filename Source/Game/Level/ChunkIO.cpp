#include "Game/Level/ChunkIO.h"

#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"

namespace NS::Game::Level
{
    bool SaveLevelToFile(const LevelData& level, const std::filesystem::path& path) noexcept
    {
        return SaveLevelToJsonFile(level, path);
    }

    bool LoadLevelFromFile(LevelData& outLevel, const std::filesystem::path& path) noexcept
    {
        if (!LoadLevelFromJsonFile(outLevel, path))
            return false;
        // 旧 "kind" だけの古いレベルを読込時に 1 度だけ実 component へ移行する。 editor / play 両経路を 1 点で覆う
        NS::Game::Blocks::MigrateLegacyLevel(outLevel);
        return true;
    }
} // namespace NS::Game::Level
