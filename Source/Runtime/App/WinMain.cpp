#include "Runtime/App/Application.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Platform/Filesystem.h"

#include <windows.h>

namespace
{
    // ロガーの初期化と終了を自動化して、途中で return しても絶対に終了処理が呼ばれるようにする
    class LoggerScope
    {
    public:
        LoggerScope()
        {
            ::NS::Core::LoggerDesc desc;
            desc.logName = "game";
            // 起動するたびに新しいログファイルを作る
            desc.rotateOnOpen = true;
#if defined(NS_SHIPPING)
            desc.logDirectory = ::NS::Platform::FileSystem::GetExeDirectory();
#else
            desc.logDirectory = ::NS::Platform::FileSystem::Combine(::NS::Platform::FileSystem::ContentRoot(), "build");
#endif
            ::NS::Core::Logger::Init(desc);
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

    std::unique_ptr<::NS::App::Application> app = ::NS::App::CreateApplication();
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
