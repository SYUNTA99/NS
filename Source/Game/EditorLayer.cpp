#include "Game/EditorLayer.h"

#include "Framework/App/Application.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Game.h"
#include "Game/LevelEditorScene.h"

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
#include <imgui.h>
#endif

EditorLayer::EditorLayer() : NS::App::Layer("EditorLayer") {}
EditorLayer::~EditorLayer() = default;

void EditorLayer::OnAttach()
{
    NS_LOG_INFO(::NS::Core::LogCat::App, "EditorLayer attached (Debug/Dev only)");
}

void EditorLayer::OnDetach()
{
    NS_LOG_INFO(::NS::Core::LogCat::App, "EditorLayer detached");
}

LevelEditorScene* EditorLayer::CurrentScene() noexcept
{
    auto* game = Game::Get();
    return game ? game->CurrentLevelEditorScene() : nullptr;
}

void EditorLayer::OnUpdate()
{
    if (!IsActive())
        return;
    auto* scene = CurrentScene();
    if (scene == nullptr)
        return;

    HandleModeToggleInput(*scene);
    HandlePauseInput(*scene);
}

void EditorLayer::OnRender()
{
    if (!IsActive())
        return;
    auto* scene = CurrentScene();
    if (scene == nullptr)
        return;

    if (scene->CurrentMode() == LevelEditorScene::Mode::Play && scene->Play().paused)
        RenderPauseModal(*scene);
}

void EditorLayer::HandleModeToggleInput(LevelEditorScene& scene) noexcept
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;
    auto& input = app->Input();

    // テキスト入力中の Tab は ImGui に渡し、 mode flip させない。
    bool wantKb = false;
    if (auto* imgui = app->ImGui())
        wantKb = imgui->WantCaptureKeyboard();

    const bool tabPressed = !wantKb && input.Keyboard().IsPressed(NS::Platform::Key::Tab);
    const bool startPressed =
        input.Gamepad(0).IsConnected() && input.Gamepad(0).IsPressed(NS::Platform::GamepadButton::Start);

    if (tabPressed || startPressed)
    {
        if (scene.CurrentMode() == LevelEditorScene::Mode::Edit)
            scene.EnterPlay();
        else
            scene.EnterEdit();
    }
}

void EditorLayer::HandlePauseInput(LevelEditorScene& scene) noexcept
{
    if (scene.CurrentMode() != LevelEditorScene::Mode::Play)
        return;
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;
    auto& input = app->Input();

    bool wantKb = false;
    if (auto* imgui = app->ImGui())
        wantKb = imgui->WantCaptureKeyboard();

    const bool pPressed = !wantKb && input.Keyboard().IsPressed(NS::Platform::Key::P);
    const bool backPressed =
        input.Gamepad(0).IsConnected() && input.Gamepad(0).IsPressed(NS::Platform::GamepadButton::Back);

    if (pPressed || backPressed)
        scene.Play().paused = !scene.Play().paused;
}

void EditorLayer::RenderPauseModal(LevelEditorScene& scene) noexcept
{
#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
    // Pause 状態は paused フラグ単独で表現するため、 ここで modal の閉じ X (右上) は不要。
    const auto vp = ImGui::GetMainViewport();
    if (vp != nullptr)
    {
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("Paused", nullptr, kFlags))
    {
        ImGui::TextUnformatted("Paused");
        ImGui::Separator();
        if (ImGui::Button("Resume", ImVec2(160.0f, 0.0f)))
            scene.Play().paused = false;
        if (ImGui::Button("Quit to Edit", ImVec2(160.0f, 0.0f)))
            scene.EnterEdit();
    }
    ImGui::End();
#else
    (void)scene;
#endif
}
