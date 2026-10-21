#include "Editor/ActionTimelinePanel.h"
#include "Editor/GameViewPanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/Timeline.h"
#include "Editor/TimelinePreview.h"
#include "Game/PlayerActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"
#include "NSlib/Windows/Mouse.h"
#include "Tests/TestActorFile.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    class TimelineGui
    {
    public:
        TimelineGui()
        {
            m_context = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{1000.0f, 800.0f};
            io.DeltaTime = 1.0f / 60.0f;
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ~TimelineGui() { ImGui::DestroyContext(m_context); }

    private:
        ImGuiContext* m_context = nullptr;
    };

    class TimelineMovingActor : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            NS::Model* model = CreateClassObj(m_model, this);
            model->SetMeshRef("cube");
            model->SetMaterialRef("Assets/Materials/player.mat");
        }
        void BodyStep() override { Root().SetPosition(Root().Position() + NS::Vector3{0.1f, 0.0f, 0.0f}); }
    };

    class TimelinePanelTestLayer : public NS::Layer
    {
    public:
        explicit TimelinePanelTestLayer(std::function<void()> check) : m_check(std::move(check)) {}
        void OnAttach() override
        {
            m_check();
            NS::Application::Quit();
        }

    private:
        std::function<void()> m_check;
    };

    std::vector<NS::uint8> ReadPixels(NS::Gfx::D3DRenderTarget& target)
    {
        ID3D11Texture2D* texture = target.Color()->Native();
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
        {
            return {};
        }
        NS::Gfx::Gpu().context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(NS::Gfx::Gpu().context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return {};
        }
        std::vector<NS::uint8> pixels;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const NS::uint8* row = static_cast<const NS::uint8*>(mapped.pData) + y * mapped.RowPitch;
            pixels.insert(pixels.end(), row, row + desc.Width * 4);
        }
        NS::Gfx::Gpu().context->Unmap(staging.Get(), 0);
        return pixels;
    }
} // namespace

TEST(EditorTimeline, SignedFramesAndFrameRateShareOneClock)
{
    const NS::Editor::TimelineFrameRange range{-10, 10};
    NS::Editor::TimelinePlayback playback;
    playback.framesPerSecond = 30.0f;
    playback.Seek(-10, range);
    playback.Toggle(range);
    playback.Tick(0.5f, range);
    EXPECT_EQ(playback.frame, 5);
    playback.Tick(0.5f, range);
    EXPECT_EQ(playback.frame, 10);
    EXPECT_FALSE(playback.playing);
    playback.Toggle(range);
    EXPECT_EQ(playback.frame, -10);
    playback.StepBy(1, range);
    EXPECT_EQ(playback.frame, -9);
    EXPECT_FALSE(playback.playing);
    EXPECT_FLOAT_EQ(playback.carry, 0.0f);
}

TEST(EditorTimeline, SeekingClearsPartialTimeAndInvalidTimeCannotMoveTheClock)
{
    const NS::Editor::TimelineFrameRange range{0, 20};
    NS::Editor::TimelinePlayback playback;
    playback.playing = true;
    playback.Tick(0.5f / 60.0f, range);
    playback.Seek(5, range);
    playback.Toggle(range);
    playback.Tick(0.5f / 60.0f, range);
    EXPECT_EQ(playback.frame, 5);
    playback.Tick(std::numeric_limits<float>::quiet_NaN(), range);
    playback.Tick(-1.0f, range);
    EXPECT_EQ(playback.frame, 5);
    playback.Seek(100, range);
    EXPECT_EQ(playback.frame, 20);
}

TEST(EditorTimelinePreview, RewindsTheEntireSceneAndDoesNotStepWhilePaused)
{
    NS::Editor::TimelinePreview preview;
    int builds = 0;
    preview.Reset(
        [&builds] {
            ++builds;
            std::unique_ptr<NS::Scene> scene = std::make_unique<NS::Scene>();
            scene->SpawnObject<TimelineMovingActor>("TimelineMovingActor");
            return scene;
        },
        {-2, 10});
    NS::Scene* scene = preview.Seek(-2);
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->IsSimulationPaused());
    NS::SceneActor* actor = *scene->Objects().begin();
    EXPECT_NEAR(actor->Root().Position().x, 0.1f, 1.0e-6f);
    scene = preview.Seek(8);
    ASSERT_NE(scene, nullptr);
    actor = *scene->Objects().begin();
    EXPECT_NEAR(actor->Root().Position().x, 1.1f, 1.0e-6f);
    EXPECT_EQ(builds, 1);
    scene = preview.Seek(-1);
    ASSERT_NE(scene, nullptr);
    actor = *scene->Objects().begin();
    EXPECT_NEAR(actor->Root().Position().x, 0.2f, 1.0e-6f);
    EXPECT_EQ(builds, 2);
    (void)preview.Seek(-1);
    scene->OnUpdate();
    EXPECT_NEAR(actor->Root().Position().x, 0.2f, 1.0e-6f);
    EXPECT_FLOAT_EQ(scene->RenderAlpha(0.0f), 1.0f);
    preview.Clear();
    EXPECT_EQ(preview.Scene(), nullptr);
    EXPECT_FALSE(preview.Frame().has_value());
    EXPECT_EQ(preview.Seek(0), nullptr);
}

TEST(EditorTimelinePreview, RenderedPixelsChangeAndRewindReproducesTheFrame)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "タイムライン描画の試し", .size = {160, 90}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    const NS::AssetManagerScope useAssets{assets};
    std::unique_ptr<NS::Gfx::D3DRenderTarget> target = NS::Gfx::D3DRenderTarget::Create({160, 90});
    ASSERT_TRUE(target->IsValid());
    NS::Editor::TimelinePreview preview;
    preview.Reset(
        [&device] {
            std::unique_ptr<NS::Scene> scene = std::make_unique<NS::Scene>();
            scene->SetDevice(&device);
            scene->SpawnObject<TimelineMovingActor>("TimelineMovingActor");
            return scene;
        },
        {0, 20});
    NS::CameraPose pose;
    pose.position = NS::Vector3{0.0f, 2.0f, -5.0f};
    pose.target = NS::Vector3{0.0f, 0.0f, 0.0f};
    const NS::SceneView view{target.get(), pose};
    device.BeginFrame();
    ASSERT_NE(preview.Seek(0), nullptr);
    ASSERT_TRUE(preview.RenderView(view));
    const std::vector<NS::uint8> first = ReadPixels(*target);
    ASSERT_FALSE(first.empty());
    ASSERT_NE(preview.Seek(15), nullptr);
    ASSERT_TRUE(preview.RenderView(view));
    const std::vector<NS::uint8> later = ReadPixels(*target);
    EXPECT_NE(first, later);
    ASSERT_NE(preview.Seek(0), nullptr);
    ASSERT_TRUE(preview.RenderView(view));
    EXPECT_EQ(first, ReadPixels(*target));
    EXPECT_TRUE(preview.RenderView(view));
    EXPECT_EQ(first, ReadPixels(*target));
    device.SetSceneTarget(nullptr);
    device.BindBackbuffer();
}

TEST(EditorTimeline, ScrubbingAnUnrelatedTrackUsesTheSameSignedClock)
{
    TimelineGui gui;
    NS::Editor::TimelinePlayback playback;
    playback.playing = true;
    std::optional<std::size_t> selected;
    const std::vector<NS::Editor::TimelineTrack> tracks{{"音量", -5, 20, {}}, {"カメラの位置", 0, 10, {2}}};
    const NS::Editor::TimelineFrameRange range{-5, 20};
    std::optional<NS::Editor::TimelineFrameRange> seekRange;
    ImVec2 click{};
    const auto draw = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{800.0f, 500.0f});
        ImGui::Begin("共通の時間軸");
        const bool moved = NS::Editor::DrawTimelineTracks(
            tracks,
            range,
            playback,
            selected,
            true,
            0.0f,
            [&click](const NS::Editor::TimelineLayout& layout) {
                const ImVec2 window = ImGui::GetWindowPos();
                const ImVec2 origin = ImGui::GetCursorStartPos();
                click = ImVec2{window.x + origin.x + layout.labelWidth +
                                   (7.0f - layout.firstFrame + 0.5f) * layout.frameWidth,
                               window.y + origin.y + layout.rowHeight * 0.5f};
            },
            seekRange);
        ImGui::End();
        ImGui::EndFrame();
        return moved;
    };
    (void)draw();
    ImGui::GetIO().AddMousePosEvent(click.x, click.y);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    EXPECT_TRUE(draw());
    EXPECT_EQ(playback.frame, 7);
    EXPECT_FALSE(playback.playing);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    (void)draw();
    seekRange = NS::Editor::TimelineFrameRange{-5, 4};
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    EXPECT_TRUE(draw());
    EXPECT_EQ(playback.frame, 4);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    (void)draw();
}

TEST(EditorTimelinePreview, GameViewDrawsTheCurrentFrameWithoutResizingTheDisplayedTexture)
{
    TimelineGui gui;
    NS::OS::Window window(NS::OS::WindowDesc{.title = "Game 下見の試し", .size = {160, 90}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    const NS::AssetManagerScope useAssets{assets};
    NS::Scene original;
    LevelEditorController editor(&original);
    NS::Editor::GameViewPanel game;
    NS::Editor::TimelinePreview preview;
    NS::CameraPose pose;
    pose.position = NS::Vector3{0.0f, 2.0f, -5.0f};
    pose.target = NS::Vector3{0.0f, 0.0f, 0.0f};
    preview.Reset(
        [&device, pose] {
            std::unique_ptr<NS::Scene> scene = std::make_unique<NS::Scene>();
            scene->SetDevice(&device);
            scene->MainCamera()->ApplyPose(pose);
            scene->SpawnObject<TimelineMovingActor>("TimelineMovingActor");
            return scene;
        },
        {0, 20});
    const auto show = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{320.0f, 200.0f});
        game.Render(editor);
    };
    show();
    ImGui::EndFrame();
    const std::optional<NS::SceneView> view = game.CollectView(editor);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->target->IsValid());
    show();
    device.BeginFrame();
    ASSERT_NE(preview.Seek(0), nullptr);
    EXPECT_TRUE(game.RenderPreview(preview));
    const std::vector<NS::uint8> first = ReadPixels(*view->target);
    ASSERT_NE(preview.Seek(15), nullptr);
    EXPECT_TRUE(game.RenderPreview(preview));
    EXPECT_NE(first, ReadPixels(*view->target));
    ASSERT_NE(preview.Seek(0), nullptr);
    EXPECT_TRUE(game.RenderPreview(preview));
    EXPECT_EQ(first, ReadPixels(*view->target));
    ImGui::EndFrame();
    EXPECT_EQ(original.ToJson(), NS::MakeSceneJson());
    game.ResetVisibility();
    EXPECT_FALSE(game.RenderPreview(preview));
    device.SetSceneTarget(nullptr);
    device.BindBackbuffer();
}

TEST(EditorGameCursor, EscapeThenGameImageClickRecapturesTheCursor)
{
    NS::ApplicationDesc desc;
    desc.window.title = "Game のカーソル復帰の試し";
    desc.window.size = {640, 400};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    NS::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    const NS::AssetManagerScope useAssets{assets};
    TimelineGui gui;
    NS::Scene scene;
    LevelEditorController editor(&scene);
    NS::Editor::GameViewPanel game;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({640.0f, 400.0f});
        game.Render(editor);
        ImGui::EndFrame();
        (void)game.CollectView(editor);
    };
    editor.EnterPlay();
    ASSERT_FALSE(app.Window().IsCursorVisible());
    NS::OS::Keyboard& keyboard = app.Input().Keyboard();
    keyboard.OnKeyDown(NS::OS::Key::Escape);
    editor.Tick();
    keyboard.OnKeyUp(NS::OS::Key::Escape);
    ASSERT_TRUE(app.Window().IsCursorVisible());
    ASSERT_FALSE(app.Window().IsCursorLocked());
    ASSERT_FALSE(app.Input().Mouse().IsRelativeMode());
    for (int i = 0; i < 3; ++i)
    {
        frame();
    }
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(320.0f, 200.0f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    EXPECT_FALSE(app.Window().IsCursorVisible());
    EXPECT_TRUE(app.Window().IsCursorLocked());
    EXPECT_TRUE(app.Input().Mouse().IsRelativeMode());
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    app.SetCursorCaptured(false);
}

TEST(EditorGameCursor, EscapeThenGameTabClickRecapturesTheCursor)
{
    NS::ApplicationDesc desc;
    desc.window.title = "Game タブのカーソル復帰の試し";
    desc.window.size = {640, 400};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    NS::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    const NS::AssetManagerScope useAssets{assets};
    TimelineGui gui;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    NS::Scene scene;
    LevelEditorController editor(&scene);
    NS::Editor::GameViewPanel game;
    const ImGuiID dock = ImHashStr("カーソル復帰のドック");
    bool built = false;
    const auto frame = [&] {
        ImGui::NewFrame();
        if (!built)
        {
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock, io.DisplaySize);
            ImGui::DockBuilderDockWindow(NS::Editor::k_PanelGame, dock);
            ImGui::DockBuilderDockWindow(NS::Editor::k_PanelScene, dock);
            ImGui::DockBuilderFinish(dock);
            built = true;
        }
        ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport());
        ImGui::Begin(NS::Editor::k_PanelScene);
        ImGui::End();
        game.Render(editor);
        ImGui::EndFrame();
        (void)game.CollectView(editor);
    };
    editor.EnterPlay();
    for (int i = 0; i < 3; ++i)
    {
        frame();
    }
    ImGui::SetWindowFocus(NS::Editor::k_PanelGame);
    frame();
    frame();
    NS::OS::Keyboard& keyboard = app.Input().Keyboard();
    keyboard.OnKeyDown(NS::OS::Key::Escape);
    editor.Tick();
    keyboard.OnKeyUp(NS::OS::Key::Escape);
    ASSERT_TRUE(app.Window().IsCursorVisible());
    ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelGame);
    ASSERT_NE(window, nullptr);
    ASSERT_NE(window->DockNode, nullptr);
    const ImVec2 tab = window->DC.DockTabItemRect.GetCenter();
    io.AddMousePosEvent(tab.x, tab.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    EXPECT_FALSE(app.Window().IsCursorVisible());
    EXPECT_TRUE(app.Window().IsCursorLocked());
    EXPECT_TRUE(app.Input().Mouse().IsRelativeMode());
    app.SetCursorCaptured(false);

    const auto clickTab = [&] {
        const ImVec2 at = window->DC.DockTabItemRect.GetCenter();
        io.AddMousePosEvent(at.x, at.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    };
    scene.SetSimulationPaused(true);
    clickTab();
    EXPECT_TRUE(app.Window().IsCursorVisible());
    EXPECT_FALSE(app.Window().IsCursorLocked());
    EXPECT_FALSE(app.Input().Mouse().IsRelativeMode());
    scene.SetSimulationPaused(false);

    ImGui::SetWindowFocus(NS::Editor::k_PanelScene);
    frame();
    frame();
    clickTab();
    EXPECT_FALSE(app.Window().IsCursorVisible());
    EXPECT_TRUE(app.Window().IsCursorLocked());
    EXPECT_TRUE(app.Input().Mouse().IsRelativeMode());
    app.SetCursorCaptured(false);

    const ImVec2 start = window->DC.DockTabItemRect.GetCenter();
    io.AddMousePosEvent(start.x, start.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    EXPECT_TRUE(app.Window().IsCursorVisible());
    io.AddMousePosEvent(start.x + 35.0f, start.y + 80.0f);
    frame();
    frame();
    EXPECT_TRUE(app.Window().IsCursorVisible());
    EXPECT_NE(ImGui::GetDragDropPayload(), nullptr);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    EXPECT_TRUE(app.Window().IsCursorVisible());
    EXPECT_FALSE(app.Window().IsCursorLocked());
    EXPECT_FALSE(app.Input().Mouse().IsRelativeMode());
}

// 選んだアクターの .actor の動作を出し、足した事象と数で直した始まりが下見の動作に効く
TEST(EditorActionTimelinePanel, PlaybackAndEditsReachThePreviewThroughThePanel)
{
    // 窓は読んである .actor の文書を直すので、終わったら元の文書へ戻す
    const ScopedActorFileEdit restore("PlayerActor", [](nlohmann::json&) {});
    NS::ApplicationDesc desc;
    desc.window.title = "タイムラインの接続の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TimelinePanelTestLayer>([&app] {
        TimelineGui gui;
        NS::Scene scene;
        scene.SetDevice(&app.Device());
        nlohmann::json snapshot = NS::MakeSceneJson();
        nlohmann::json floor = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(floor, "MapPartsActor");
        NS::SetObjectJsonId(floor, 100);
        NS::SetObjectPosition(floor, {0.0f, -0.5f, 0.0f});
        NS::SetObjectScale(floor, {60.0f, 1.0f, 60.0f});
        NS::SceneJsonObjects(snapshot).push_back(std::move(floor));
        nlohmann::json player = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(player, "PlayerActor");
        NS::SetObjectJsonId(player, 1);
        NS::SetObjectPosition(player, {0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(snapshot).push_back(std::move(player));
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, "PropActor");
        NS::SetObjectJsonId(rock, 2);
        NS::SetObjectPosition(rock, {0.0f, 1.5f, 7.0f});
        NS::SetObjectScale(rock, {3.0f, 3.0f, 3.0f});
        NS::SceneJsonObjects(snapshot).push_back(std::move(rock));
        scene.ResetFromJson(std::move(snapshot));
        scene.SetSimulationEnabled(false);
        LevelEditorController editor(&scene);
        editor.SelectObjectById(1);
        NS::Editor::GameViewPanel game;
        NS::Editor::ActionTimelinePanel timeline;
        const auto begin = [&] {
            ImGui::NewFrame();
            timeline.Tick(editor, 0.0f);
            ImGui::SetNextWindowPos({0.0f, 0.0f});
            ImGui::SetNextWindowSize({320.0f, 200.0f});
            game.Render(editor);
        };
        const auto drawPanel = [&] {
            ImGui::SetNextWindowPos({330.0f, 0.0f});
            ImGui::SetNextWindowSize({650.0f, 780.0f});
            ImGui::SetNextWindowCollapsed(false);
            timeline.Render(editor);
        };
        const auto drawPanelCollapsed = [&] {
            ImGui::SetNextWindowCollapsed(true);
            timeline.Render(editor);
        };
        const auto activate = [](const char* label) {
            ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelActionTimeline);
            ASSERT_NE(window, nullptr);
            ImGuiContext& context = *ImGui::GetCurrentContext();
            context.NavActivateId = window->GetID(label);
            context.NavActivateDownId = context.NavActivateId;
        };
        // 選んだ行の数の欄へ打ち込んで決める。欄は行の欄の表 ##event-row の中にある
        const auto typeInto = [&](const char* field, const char* text) {
            ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelActionTimeline);
            ASSERT_NE(window, nullptr);
            const ImGuiID id = ImHashStr(field, 0, ImHashStr("##event-row", 0, window->ID));
            ImGui::ActivateItemByID(id);
            GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
            begin();
            drawPanel();
            ImGui::EndFrame();
            ASSERT_EQ(GImGui->ActiveId, id);
            ImGui::GetIO().AddInputCharactersUTF8(text);
            begin();
            drawPanel();
            ImGui::EndFrame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            begin();
            drawPanel();
            ImGui::EndFrame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            begin();
            drawPanel();
            ImGui::EndFrame();
        };
        // 下見を frame まで進め、自機の当たる直前の動作がそのフレームに row の事象を始めたか
        const auto startedAt = [&](int frame, std::size_t row) {
            NS::Editor::TimelinePreview* seeking = timeline.Preview(editor);
            EXPECT_NE(seeking, nullptr);
            if (seeking == nullptr)
            {
                return false;
            }
            NS::Scene* seeked = seeking->Seek(frame);
            EXPECT_NE(seeked, nullptr);
            if (seeked == nullptr)
            {
                return false;
            }
            const NS::SceneActor* player = seeked->Objects().FindByObjectId(1);
            EXPECT_NE(player, nullptr);
            if (player == nullptr || player->ActionTimelineClassObj() == nullptr)
            {
                return false;
            }
            const NS::ActionTimeline& actions = *player->ActionTimelineClassObj();
            EXPECT_EQ(actions.CurrentName(), "BeforeContactCenter");
            EXPECT_EQ(actions.Frame(), frame);
            const std::vector<std::size_t>& started = actions.StartedEvents();
            return std::find(started.begin(), started.end(), row) != started.end();
        };
        begin();
        drawPanel();
        ImGui::EndFrame();
        // 一覧は選んだ自機の .actor の動作
        EXPECT_EQ(timeline.EditingActorFile(), "PlayerActor");
        const std::vector<NS::ActorAction> shipped =
            NS::ReadActorActions("PlayerActor", *NS::ActorFileLibrary::Get().Find("PlayerActor"));
        ASSERT_FALSE(shipped.empty());
        ASSERT_NE(timeline.EditingAction(), nullptr);
        EXPECT_EQ(timeline.EditingAction()->name, shipped.front().name);
        EXPECT_EQ(timeline.EditingAction()->events.size(), shipped.front().events.size());
        const std::optional<NS::SceneView> view = game.CollectView(editor);
        ASSERT_TRUE(view.has_value());
        begin();
        EXPECT_EQ(timeline.Preview(editor), nullptr);
        activate("再生");
        drawPanel();
        NS::Editor::TimelinePreview* preview = timeline.Preview(editor);
        ASSERT_NE(preview, nullptr);
        ASSERT_TRUE(preview->Frame().has_value());
        const int firstFrame = *preview->Frame();
        EXPECT_EQ(firstFrame, 0);
        const nlohmann::json original = scene.ToJson();
        EXPECT_TRUE(game.RenderPreview(*preview));
        const std::vector<NS::uint8> firstPixels = ReadPixels(*view->target);
        ImGui::EndFrame();
        begin();
        timeline.Tick(editor, 0.5f);
        preview = timeline.Preview(editor);
        ASSERT_NE(preview, nullptr);
        EXPECT_GT(*preview->Frame(), firstFrame);
        EXPECT_TRUE(game.RenderPreview(*preview));
        EXPECT_NE(firstPixels, ReadPixels(*view->target));
        EXPECT_EQ(scene.ToJson(), original);
        activate("止める");
        drawPanel();
        ImGui::EndFrame();
        begin();
        const int pausedFrame = *timeline.Preview(editor)->Frame();
        timeline.Tick(editor, 0.5f);
        EXPECT_EQ(*timeline.Preview(editor)->Frame(), pausedFrame);
        activate("<");
        drawPanel();
        EXPECT_EQ(*timeline.Preview(editor)->Frame(), pausedFrame - 1);
        ImGui::EndFrame();
        // パネルを畳んでも、Game に下見を出していれば配置の変更は下見に届く
        begin();
        drawPanelCollapsed();
        ImGui::EndFrame();
        NS::SceneActor* target = scene.Objects().FindByObjectId(2);
        ASSERT_NE(target, nullptr);
        target->Root().SetPosition({2.0f, 1.5f, 7.0f});
        scene.SyncPhysics();
        begin();
        preview = timeline.Preview(editor);
        ASSERT_NE(preview, nullptr);
        NS::Scene* updated = preview->Seek(0);
        ASSERT_NE(updated, nullptr);
        ASSERT_NE(updated->Objects().FindByObjectId(2), nullptr);
        EXPECT_NEAR(updated->Objects().FindByObjectId(2)->Root().Position().x, 2.0f, 0.01f);
        activate("<");
        drawPanel();
        ImGui::EndFrame();
        begin();
        activate("<");
        drawPanel();
        ImGui::EndFrame();
        begin();
        ASSERT_NE(timeline.Preview(editor), nullptr);
        // 当たる直前の動作は 0〜6 フレームで、6 は検知のフレーム。足す所はその前に置く
        const int addFrame = *timeline.Preview(editor)->Frame();
        ASSERT_GE(addFrame, 3);
        ASSERT_LT(addFrame, 6);
        const std::size_t addedRow = timeline.EditingAction()->events.size();
        activate("再生の位置に足す");
        drawPanel();
        ASSERT_NE(timeline.Preview(editor), nullptr);
        const int editingFrame = *timeline.Preview(editor)->Frame();
        timeline.Tick(editor, 0.5f);
        EXPECT_EQ(*timeline.Preview(editor)->Frame(), editingFrame);
        ImGui::EndFrame();
        ASSERT_EQ(timeline.EditingAction()->events.size(), addedRow + 1);
        EXPECT_EQ(timeline.EditingAction()->events[addedRow].start, addFrame);
        begin();
        drawPanel();
        ImGui::EndFrame();
        begin();
        EXPECT_TRUE(startedAt(addFrame, addedRow));
        EXPECT_FALSE(startedAt(1, addedRow));
        drawPanel();
        ImGui::EndFrame();
        // 選んだ行の始まりと長さを数で直すと、その場で .actor の文書と下見の動作に効く
        typeInto("##start", "1");
        typeInto("##length", "3");
        ASSERT_EQ(timeline.EditingAction()->events.size(), addedRow + 1);
        EXPECT_EQ(timeline.EditingAction()->events[addedRow].start, 1);
        EXPECT_EQ(timeline.EditingAction()->events[addedRow].length, 3);
        const std::vector<NS::ActorAction> edited =
            NS::ReadActorActions("PlayerActor", *NS::ActorFileLibrary::Get().Find("PlayerActor"));
        ASSERT_FALSE(edited.empty());
        ASSERT_EQ(edited.front().events.size(), addedRow + 1);
        EXPECT_EQ(edited.front().events[addedRow].start, 1);
        EXPECT_EQ(edited.front().events[addedRow].length, 3);
        begin();
        drawPanel();
        ImGui::EndFrame();
        begin();
        EXPECT_TRUE(startedAt(1, addedRow));
        EXPECT_FALSE(startedAt(addFrame, addedRow));
        drawPanel();
        ImGui::EndFrame();
        begin();
        ASSERT_NE(timeline.Preview(editor), nullptr);
        EXPECT_EQ(scene.Objects().FindByObjectId(2)->Root().Position().x, 2.0f);
        activate("Game に下見を表示");
        drawPanel();
        EXPECT_EQ(timeline.Preview(editor), nullptr);
        ImGui::EndFrame();
        app.Device().BindBackbuffer();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 「保存」は開いていない .actor の変更も書く。今の .actor だけ書いて印を消すと、選び直す前の変更が黙って失われる
TEST(EditorActionTimelinePanel, SaveWritesEveryEditedActorFile)
{
    const ScopedActorFileDirectory directory("EditorActionTimelineSave");
    const std::string shippedDirectory =
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Actors");
    for (const char* name : {"PlayerActor", "PropActor"})
    {
        const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(
            NS::OS::FileSystem::Combine(shippedDirectory, std::string{name} + ".actor"));
        ASSERT_TRUE(text.has_value()) << name;
        WriteActorFile(directory.Path(), name, *text);
    }
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    ASSERT_NE(library.Find("PlayerActor"), nullptr);
    const std::vector<NS::ActorAction> before = NS::ReadActorActions("PlayerActor", *library.Find("PlayerActor"));
    ASSERT_FALSE(before.empty());

    TimelineGui gui;
    NS::Scene scene;
    nlohmann::json snapshot = NS::MakeSceneJson();
    nlohmann::json player = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(player, "PlayerActor");
    NS::SetObjectJsonId(player, 1);
    NS::SceneJsonObjects(snapshot).push_back(std::move(player));
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(rock, "PropActor");
    NS::SetObjectJsonId(rock, 2);
    NS::SetObjectPosition(rock, {0.0f, 1.5f, 7.0f});
    NS::SceneJsonObjects(snapshot).push_back(std::move(rock));
    scene.ResetFromJson(std::move(snapshot));
    scene.SetSimulationEnabled(false);
    LevelEditorController editor(&scene);
    NS::Editor::ActionTimelinePanel timeline;

    const auto frame = [&](const char* press) {
        ImGui::NewFrame();
        if (press != nullptr)
        {
            ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelActionTimeline);
            ASSERT_NE(window, nullptr);
            ImGuiContext& context = *ImGui::GetCurrentContext();
            context.NavActivateId = window->GetID(press);
            context.NavActivateDownId = context.NavActivateId;
        }
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({650.0f, 780.0f});
        timeline.Render(editor);
        ImGui::EndFrame();
    };
    // 自機の .actor の動作に事象を足してから置物を選び直し、置物の .actor を開いた窓で保存する
    editor.SelectObjectById(1);
    frame(nullptr);
    frame("再生の位置に足す");
    editor.SelectObjectById(2);
    frame(nullptr);
    EXPECT_EQ(timeline.EditingActorFile(), "PropActor");
    frame("保存");

    library.Reload();
    ASSERT_NE(library.Find("PlayerActor"), nullptr);
    const std::vector<NS::ActorAction> saved = NS::ReadActorActions("PlayerActor", *library.Find("PlayerActor"));
    ASSERT_EQ(saved.size(), before.size());
    EXPECT_EQ(saved.front().events.size(), before.front().events.size() + 1);
}
