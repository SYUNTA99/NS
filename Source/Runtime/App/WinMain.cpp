#include "Runtime/App/Application.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <windows.h>

namespace
{
    // ロガーの初期化と終了を自動化して、途中で return しても絶対に終了処理が呼ばれるようにする
    class LoggerScope
    {
    public:
        LoggerScope()
        {
            ::NS::Core::Logger::SetLogName("game");
            // 起動するたびに新しいログファイルを作る
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

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    LoggerScope loggerScope;

    auto app = ::NS::App::CreateApplication();
    if (!app)
    {
        NS_LOG_ERROR(App, "WinMain: CreateApplication が nullptr");
        return -1;
    }
    if (!app->IsValid())
    {
        NS_LOG_ERROR(App, "WinMain: Application 構築失敗");
        return -1;
    }

    return app->Run();
}
