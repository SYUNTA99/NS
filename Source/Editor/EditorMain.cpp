#include "Editor/ActorFileGenerator.h"
#include "Editor/Editor.h"
#include "Game/Game.h"
#include "NSlib/App/Application.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"

namespace NS
{

    std::unique_ptr<Application> CreateApplication()
    {
        // コードに足した項目を .actor へ書き足してから読む。ActorFileLibrary が初めて読むのは Game の層を積んだ後
        (void)Editor::GenerateActorFiles(ActorFileLibrary::Get().Directory());

        ApplicationDesc desc{};
        desc.window.title = "NS Editor";
        desc.window.size = Size2D{1920, 1080};
        desc.enableAudio = true;
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
