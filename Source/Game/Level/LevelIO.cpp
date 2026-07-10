#include "Game/Level/LevelIO.h"

#include "Game/Level/LevelJson.h"

namespace NS::Game::Level
{
    bool SaveLevelToFile(const NS::Scene::SceneData& level, const std::filesystem::path& path) noexcept
    {
        return SaveLevelToJsonFile(level, path);
    }

    bool LoadLevelFromFile(NS::Scene::SceneData& outLevel, const std::filesystem::path& path, LevelLoadReport* outReport) noexcept
    {
        return LoadLevelFromJsonFile(outLevel, path, outReport);
    }
} // namespace NS::Game::Level
