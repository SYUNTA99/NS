#include "Framework/App/Application.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include "Framework/Framework.h"

namespace
{
    // Logger Init/Shutdown を RAII で対応付け、早期 return での Shutdown 漏れを防ぐ
    class LoggerScope
    {
    public:
        LoggerScope()
        {
            ::NS::Core::Logger::SetLogName("game");
            // 起動ごとにローテートし 1 セッション = 1 ファイルにする
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

    // Layer / overlay の構成は CreateApplication (Game 側) が済ませている。 WinMain は Game / Editor へ依存しない
    return app->Run();
}
