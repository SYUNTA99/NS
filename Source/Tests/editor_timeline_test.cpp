#include "Editor/GameViewPanel.h"
#include "Editor/HitTimelinePanel.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/Timeline.h"
#include "Editor/TimelinePreview.h"
#include "Game/Player.h"
#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/RenderTarget.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Graphics/Texture.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"
#include "Tests/TestHitTimelines.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <memory>
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
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ~TimelineGui() { ImGui::DestroyContext(m_context); }

    private:
        ImGuiContext* m_context = nullptr;
    };

    class TimelineMovingActor : public NS::Obj::Actor
    {
    protected:
        void OnInit() override
        {
            NS::Obj::Model* model = CreateSubObj<NS::Obj::Model>(ModelSlot());
            model->SetMeshRef("cube");
            model->SetMaterialRef("player");
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

    std::vector<std::uint8_t> ReadPixels(NS::Gfx::RenderTarget& target)
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
        std::vector<std::uint8_t> pixels;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const std::uint8_t* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
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
            std::unique_ptr<NS::Obj::Scene> scene = std::make_unique<NS::Obj::Scene>();
            scene->SpawnTransient<TimelineMovingActor>();
            return scene;
        },
        {-2, 10});
    NS::Obj::Scene* scene = preview.Seek(-2);
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->IsSimulationPaused());
    NS::Obj::Actor* actor = *scene->Objects().begin();
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
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());
    NS::Obj::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    assets.RegisterBuiltins();
    assets.RegisterSharedMaterials();
    std::unique_ptr<NS::Gfx::RenderTarget> target = NS::Gfx::RenderTarget::Create({160, 90});
    ASSERT_TRUE(target->IsValid());
    NS::Editor::TimelinePreview preview;
    preview.Reset(
        [&assets, &renderer] {
            std::unique_ptr<NS::Obj::Scene> scene = std::make_unique<NS::Obj::Scene>();
            scene->SetAssets(&assets);
            scene->SetRenderer(&renderer);
            scene->SpawnTransient<TimelineMovingActor>();
            return scene;
        },
        {0, 20});
    NS::Obj::CameraPose pose;
    pose.position = NS::Vector3{0.0f, 2.0f, -5.0f};
    pose.target = NS::Vector3{0.0f, 0.0f, 0.0f};
    const NS::Obj::SceneView view{target.get(), pose};
    renderer.BeginFrame();
    ASSERT_NE(preview.Seek(0), nullptr);
    ASSERT_TRUE(preview.RenderView(view));
    const std::vector<std::uint8_t> first = ReadPixels(*target);
    ASSERT_FALSE(first.empty());
    ASSERT_NE(preview.Seek(15), nullptr);
    ASSERT_TRUE(preview.RenderView(view));
    const std::vector<std::uint8_t> later = ReadPixels(*target);
    EXPECT_NE(first, later);
    ASSERT_NE(preview.Seek(0), nullptr);
    ASSERT_TRUE(preview.RenderView(view));
    EXPECT_EQ(first, ReadPixels(*target));
    EXPECT_TRUE(preview.RenderView(view));
    EXPECT_EQ(first, ReadPixels(*target));
    renderer.SetSceneTarget(nullptr);
    renderer.BindBackbuffer();
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
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());
    NS::Obj::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    assets.RegisterBuiltins();
    assets.RegisterSharedMaterials();
    NS::Obj::Scene original;
    LevelEditorController editor(&original);
    NS::Editor::GameViewPanel game;
    NS::Editor::TimelinePreview preview;
    NS::Obj::CameraPose pose;
    pose.position = NS::Vector3{0.0f, 2.0f, -5.0f};
    pose.target = NS::Vector3{0.0f, 0.0f, 0.0f};
    preview.Reset(
        [&assets, &renderer, pose] {
            std::unique_ptr<NS::Obj::Scene> scene = std::make_unique<NS::Obj::Scene>();
            scene->SetAssets(&assets);
            scene->SetRenderer(&renderer);
            scene->MainCamera()->ApplyPose(pose);
            scene->SpawnTransient<TimelineMovingActor>();
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
    const std::optional<NS::Obj::SceneView> view = game.CollectView(editor);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->target->IsValid());
    show();
    renderer.BeginFrame();
    ASSERT_NE(preview.Seek(0), nullptr);
    EXPECT_TRUE(game.RenderPreview(preview));
    const std::vector<std::uint8_t> first = ReadPixels(*view->target);
    ASSERT_NE(preview.Seek(15), nullptr);
    EXPECT_TRUE(game.RenderPreview(preview));
    EXPECT_NE(first, ReadPixels(*view->target));
    ASSERT_NE(preview.Seek(0), nullptr);
    EXPECT_TRUE(game.RenderPreview(preview));
    EXPECT_EQ(first, ReadPixels(*view->target));
    ImGui::EndFrame();
    EXPECT_EQ(original.ToJson(), NS::Obj::MakeSceneJson());
    game.ResetVisibility();
    EXPECT_FALSE(game.RenderPreview(preview));
    renderer.SetSceneTarget(nullptr);
    renderer.BindBackbuffer();
}

TEST(EditorHitTimelinePanel, PlaybackAndEditsReachThePreviewThroughThePanel)
{
    const ScopedHitTimelineDirectory directory("EditorTimelinePanel");
    ScopedHitTimelineDirectory::SetBothTiers(MakeLegacyHitTimeline(2));
    NS::ApplicationDesc desc;
    desc.window.title = "タイムラインの接続の試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TimelinePanelTestLayer>([&app] {
        TimelineGui gui;
        NS::Obj::Scene scene;
        scene.SetAssets(&app.Assets());
        scene.SetRenderer(&app.Renderer());
        nlohmann::json snapshot = NS::Obj::MakeSceneJson();
        nlohmann::json floor = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(floor, "MapParts");
        NS::Obj::SetObjectJsonId(floor, 100);
        NS::Obj::SetObjectPosition(floor, {0.0f, -0.5f, 0.0f});
        NS::Obj::SetObjectScale(floor, {60.0f, 1.0f, 60.0f});
        NS::Obj::SceneJsonObjects(snapshot).push_back(std::move(floor));
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, {0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(snapshot).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, {0.0f, 1.5f, 7.0f});
        NS::Obj::SetObjectScale(rock, {3.0f, 3.0f, 3.0f});
        NS::Obj::SceneJsonObjects(snapshot).push_back(std::move(rock));
        scene.LoadJson(std::move(snapshot));
        scene.SetSimulationEnabled(false);
        LevelEditorController editor(&scene);
        NS::Editor::GameViewPanel game;
        NS::Editor::HitTimelinePanel timeline;
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
            ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelHitTimeline);
            ASSERT_NE(window, nullptr);
            ImGuiContext& context = *ImGui::GetCurrentContext();
            context.NavActivateId = window->GetID(label);
            context.NavActivateDownId = context.NavActivateId;
        };
        begin();
        drawPanel();
        ImGui::EndFrame();
        const std::optional<NS::Obj::SceneView> view = game.CollectView(editor);
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
        const std::vector<std::uint8_t> firstPixels = ReadPixels(*view->target);
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
        NS::Obj::Actor* target = scene.Objects().FindByObjectId(2);
        ASSERT_NE(target, nullptr);
        target->Root().SetPosition({2.0f, 1.5f, 7.0f});
        scene.SyncPhysics();
        begin();
        preview = timeline.Preview(editor);
        ASSERT_NE(preview, nullptr);
        NS::Obj::Scene* updated = preview->Seek(0);
        ASSERT_NE(updated, nullptr);
        ASSERT_NE(updated->Objects().FindByObjectId(2), nullptr);
        EXPECT_NEAR(updated->Objects().FindByObjectId(2)->Root().Position().x, 2.0f, 0.01f);
        Player* previewPlayer = FindPlayer(preview->Scene()->Objects());
        ASSERT_NE(previewPlayer, nullptr);
        const std::size_t addedRow = MakeLegacyHitTimeline(2).events.size();
        const std::vector<std::size_t>& beforeRows = previewPlayer->Resolver().RowsStartedThisStep();
        EXPECT_EQ(std::find(beforeRows.begin(), beforeRows.end(), addedRow), beforeRows.end());
        activate("再生の位置に足す");
        drawPanel();
        ASSERT_NE(timeline.Preview(editor), nullptr);
        const int editingFrame = *preview->Frame();
        timeline.Tick(editor, 0.5f);
        EXPECT_EQ(*timeline.Preview(editor)->Frame(), editingFrame);
        ImGui::EndFrame();
        begin();
        drawPanel();
        ImGui::EndFrame();
        begin();
        ASSERT_NE(timeline.Preview(editor), nullptr);
        previewPlayer = FindPlayer(timeline.Preview(editor)->Scene()->Objects());
        ASSERT_NE(previewPlayer, nullptr);
        const std::vector<std::size_t>& afterRows = previewPlayer->Resolver().RowsStartedThisStep();
        EXPECT_NE(std::find(afterRows.begin(), afterRows.end(), addedRow), afterRows.end());
        EXPECT_EQ(scene.Objects().FindByObjectId(2)->Root().Position().x, 2.0f);
        activate("Game に下見を表示");
        drawPanel();
        EXPECT_EQ(timeline.Preview(editor), nullptr);
        ImGui::EndFrame();
        app.Renderer().BindBackbuffer();
    }));
    EXPECT_EQ(app.Run(), 0);
}

// 「保存」は開いていない段の変更も書く。今の段だけ書いて印を消すと、もう片方の段の変更が黙って失われる
TEST(EditorHitTimelinePanel, SaveWritesEveryTier)
{
    const ScopedHitTimelineDirectory directory("EditorTimelineSave");
    ScopedHitTimelineDirectory::SetBothTiers(MakeLegacyHitTimeline(2));
    NS::Game::Level::HitTimelineLibrary& library = NS::Game::Level::HitTimelineLibrary::Get();
    TimelineGui gui;
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);
    NS::Editor::HitTimelinePanel timeline;
    // パネルは真ん中の段を開いている。外れの段は置き場にだけ変更がある
    NS::Game::Level::HitTimeline miss = MakeLegacyHitTimeline(2);
    miss.events.pop_back();
    library.Set("miss", miss);

    const auto drawPanel = [&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({650.0f, 780.0f});
        timeline.Render(editor);
    };
    ImGui::NewFrame();
    drawPanel();
    ImGui::EndFrame();
    ImGui::NewFrame();
    ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelHitTimeline);
    ASSERT_NE(window, nullptr);
    ImGuiContext& context = *ImGui::GetCurrentContext();
    context.NavActivateId = window->GetID("保存");
    context.NavActivateDownId = context.NavActivateId;
    drawPanel();
    ImGui::EndFrame();

    library.Reload();
    ASSERT_NE(library.Find("center"), nullptr);
    const NS::Game::Level::HitTimeline* saved = library.Find("miss");
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->events.size(), miss.events.size());
}
