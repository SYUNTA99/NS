#include "Editor/LevelFilePaths.h"

#include "Game/Game.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

namespace NS
{
    namespace Editor
    {
        std::string StartLevelName() noexcept
        {
            return QualifyLevelPath(SanitizeLevelPath(::Game::StartSceneName()));
        }

        bool EnsureScenesDirectoryExists() noexcept
        {
            const std::string dir = OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets/Scenes");
            if (OS::FileSystem::Exists(dir))
            {
                return true;
            }
            if (!OS::FileSystem::CreateDirectories(dir))
            {
                NS_LOG_ERROR(App, "Scenes/ ディレクトリ作成失敗");
                return false;
            }
            return true;
        }
    } // namespace Editor
} // namespace NS
