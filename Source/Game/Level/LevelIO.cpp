#include "Game/Level/LevelIO.h"

#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"
#include "Game/PlayerTuning.h"

namespace NS::Game::Level
{
    bool SaveLevelToFile(const LevelData& level, const std::filesystem::path& path) noexcept
    {
        return SaveLevelToJsonFile(level, path);
    }

    bool LoadLevelFromFile(LevelData& outLevel, const std::filesystem::path& path) noexcept
    {
        LevelLoadReport report{};
        if (!LoadLevelFromJsonFile(outLevel, path, &report))
            return false;

        // 旧形式から合成したプレイヤーには保存済みテンプレートの構成と値を写し、 移行前の手触りを保つ
        if (report.playerObjectCreated)
        {
            const std::size_t playerIndex = FindPlayerObjectIndex(outLevel);
            if (playerIndex != kNoObjectIndex)
                MergeSavedPlayerTuning(outLevel.objects[playerIndex]);
        }
        return true;
    }
} // namespace NS::Game::Level
