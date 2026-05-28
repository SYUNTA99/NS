#include "Framework/App/Application.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Game/Game.h"

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
#include "Game/EditorLayer.h"
#endif

#include "Framework/Framework.h"

#include <memory>

namespace
{
    /// Logger の Init / Shutdown を RAII で対応付けて、早期 return の追加で Shutdown
    /// 呼び忘れを起こさないようにする。
    class LoggerScope
    {
    public:
        LoggerScope()
        {
            ::NS::Core::Logger::SetLogName("game");
            // Game は起動ごとに rotate して 1 セッション = 1 ファイル運用にする。
            ::NS::Core::Logger::SetRotateOnOpen(true);
            ::NS::Core::Logger::Init();
        }
        ~LoggerScope() { ::NS::Core::Logger::Shutdown(); }

        LoggerScope(const LoggerScope&) = delete;
        LoggerScope& operator=(const LoggerScope&) = delete;
        LoggerScope(LoggerScope&&) = delete;
        LoggerScope& operator=(LoggerScope&&) = delete;
    };
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    LoggerScope loggerScope;

    auto app = ::NS::App::CreateApplication();
    if (!app)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::App, "WinMain: CreateApplication が nullptr");
        return -1;
    }
    if (!app->IsValid())
    {
        NS_LOG_ERROR(::NS::Core::LogCat::App, "WinMain: Application 構築失敗");
        return -1;
    }

    app->AddLayer(std::make_unique<Game>());

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
    app->AddOverlay(std::make_unique<EditorLayer>());
#endif

    return app->Run();
}
