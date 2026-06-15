#include "Game/EditorLayer.h"

#include "Framework/App/Application.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/RenderSettings.h"
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

    if (scene->CurrentMode() == LevelEditorScene::Mode::Edit)
    {
        scene->Editor().RenderFileBrowser();
        RenderToolModePanel(*scene);
    }
    else if (scene->Play().paused)
        RenderPauseModal(*scene);

    RenderFpsOverlay();
    RenderRenderSettingsPanel(*scene);
}

void EditorLayer::HandleModeToggleInput(LevelEditorScene& scene) noexcept
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;
    auto& input = app->Input();

    // ImGui がキーボードを握っている間は mode flip させない
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

void EditorLayer::RenderFpsOverlay() noexcept
{
#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
    const auto vp = ImGui::GetMainViewport();
    if (vp == nullptr)
        return;
    // pivot=(1,0) で右端固定。 width が不確定でも右端から padding 分だけ内側に収まる
    constexpr float kPadding = 10.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kPadding, vp->WorkPos.y + kPadding),
                            ImGuiCond_Always,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##FpsOverlay", nullptr, kFlags))
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float fps = io.Framerate;
        const float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
        ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(fps), static_cast<double>(ms));
    }
    ImGui::End();
#endif
}

void EditorLayer::RenderRenderSettingsPanel(LevelEditorScene& scene) noexcept
{
#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
    const NS::Graphics::RenderSettings& resolved = scene.DebugResolvedSettings();
    const NS::Graphics::RenderSettingsOverride& sceneOver = scene.DebugSceneOverride();
    const NS::Graphics::RenderSettingsOverride& objOver = scene.DebugPlayerObjectOverride();

    // 出所は has_value の突き合わせで逆算する。 Resolve のホットパスに追跡を入れない
    auto provenance = [](bool sceneHas, bool objectHas) -> const char* {
        if (objectHas)
            return "object";
        if (sceneHas)
            return "scene";
        return "default";
    };

    if (ImGui::Begin("RenderSettings"))
    {
        ImGui::Text("lightDir   : (%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.lightDir.x),
                    static_cast<double>(resolved.lightDir.y),
                    static_cast<double>(resolved.lightDir.z),
                    provenance(sceneOver.lightDir.has_value(), objOver.lightDir.has_value()));
        ImGui::Text("lightColor : (%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.lightColor.x),
                    static_cast<double>(resolved.lightColor.y),
                    static_cast<double>(resolved.lightColor.z),
                    provenance(sceneOver.lightColor.has_value(), objOver.lightColor.has_value()));
        ImGui::Text("ambient    : (%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.ambientColor.x),
                    static_cast<double>(resolved.ambientColor.y),
                    static_cast<double>(resolved.ambientColor.z),
                    provenance(sceneOver.ambientColor.has_value(), objOver.ambientColor.has_value()));
        ImGui::Separator();
        ImGui::Text("clearColor : (%.2f,%.2f,%.2f,%.2f) [%s]",
                    static_cast<double>(resolved.clearColor.R()),
                    static_cast<double>(resolved.clearColor.G()),
                    static_cast<double>(resolved.clearColor.B()),
                    static_cast<double>(resolved.clearColor.A()),
                    provenance(sceneOver.clearColor.has_value(), objOver.clearColor.has_value()));
        ImGui::TextDisabled("clearColor / vsync のシーン上書きは非対応 (lighting 3 種のみ階層対応)");
    }
    ImGui::End();
#else
    (void)scene;
#endif
}

void EditorLayer::RenderToolModePanel(LevelEditorScene& scene) noexcept
{
#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
    const auto vp = ImGui::GetMainViewport();
    if (vp != nullptr)
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10.0f, vp->WorkPos.y + 10.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Edit Mode"))
    {
        const bool objectActive = scene.ObjectToolActive();
        if (ImGui::RadioButton("Build (Grid place)", !objectActive))
            scene.SetObjectToolActive(false);
        if (ImGui::RadioButton("Object (Gizmo)", objectActive))
            scene.SetObjectToolActive(true);
        ImGui::Separator();
        if (objectActive)
            ImGui::TextUnformatted("Click orange box to select. Q/W/E/R = Select/Move/Rotate/Scale");
        else
            ImGui::TextUnformatted("Left click = place block");
    }
    ImGui::End();
#else
    (void)scene;
#endif
}

void EditorLayer::RenderPauseModal(LevelEditorScene& scene) noexcept
{
#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
    // paused フラグ単独で状態を表現するため、 modal の閉じ X は不要
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
