#include "Editor/Editor.h"
#include "Game/Game.h"
#include "NSlib/App/Application.h"

namespace NS
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Editor";
        desc.window.size = Size2D{1920, 1080};
#ifdef NS_BUILD_DEBUG
        desc.device.enableDebugLayer = true;
#endif
        std::unique_ptr<Application> app = std::make_unique<Application>(desc);

        // 起動シーンはエディタが「開く」と同じ道で 1 回だけ読む。Game は空のシーンだけを立てる
        app->AddLayer(std::make_unique<::Game>(::Game::StartScenePath(), ::Game::StartScene::Empty));
        // プレイ中のカーソルと Esc はエディタが持つ。Game は世界だけを持つ
        app->AddOverlay(std::make_unique<::Editor>());
        return app;
    }

} // namespace NS
