#include "Editor/InspectorPanel.h"
#include "Editor/AssetsPanel.h"
#include "Editor/AssetPicker.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Editor/PanelIds.h"
#include "Editor/LevelEditorController.h"
#include "NSlib/App/Application.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/UI/ImGuiContext.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <vector>

namespace
{
    NS::ApplicationDesc PreviewAppDesc()
    {
        NS::ApplicationDesc desc;
        desc.window.title = "素材プレビューの試し";
        desc.window.size = {500, 740};
        desc.window.visible = false;
        return desc;
    }

    class EditorAssetPreview : public testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(m_app.IsValid());
            ASSERT_TRUE(m_gui.IsValid());
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        }

        std::vector<NS::uint8> Frame(bool showAssets = false)
        {
            for (int frame = 0; frame < 3; ++frame)
            {
                m_app.Device().BeginFrame();
                m_gui.BeginFrame();
                ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
                ImGui::SetNextWindowSize(ImVec2{500.0f, 740.0f});
                if (showAssets)
                {
                    m_assetsPanel.Render(m_editor);
                }
                else
                {
                    m_inspector.Render(m_editor);
                }
                m_gui.EndFrame();
                m_app.Device().EndFrame();
            }
            std::vector<NS::uint8> pixels;
            NS::Size2D size{};
            EXPECT_TRUE(m_app.Device().ReadBackbufferPixels(pixels, size));
            EXPECT_EQ(size.width, 500);
            EXPECT_EQ(size.height, 740);
            return pixels;
        }

        bool ShowsImage(ImTextureID image) const
        {
            const ImDrawData* data = ImGui::GetDrawData();
            for (int list = 0; list < data->CmdListsCount; ++list)
            {
                for (const ImDrawCmd& command : data->CmdLists[list]->CmdBuffer)
                {
                    if (command.GetTexID() == image && command.ElemCount > 0)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void Click(float x, float y)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(x, y);
            (void)Frame();
            io.AddMouseButtonEvent(0, true);
            (void)Frame();
            io.AddMouseButtonEvent(0, false);
            (void)Frame();
        }

        void OpenAssetFolder(const char* name)
        {
            (void)Frame(true);
            ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelAssets);
            ASSERT_NE(window, nullptr);
            const char* folders[] = {"Materials", "Models", "Textures"};
            for (const char* folder : folders)
            {
                window->StateStorage.SetInt(window->GetID(folder), 0);
            }
            window->StateStorage.SetInt(window->GetID(name), 1);
            (void)Frame(true);
        }

        std::vector<ImTextureID> ShownImages() const
        {
            std::vector<ImTextureID> images;
            const ImDrawData* data = ImGui::GetDrawData();
            for (int list = 0; list < data->CmdListsCount; ++list)
            {
                for (const ImDrawCmd& command : data->CmdLists[list]->CmdBuffer)
                {
                    const ImTextureID texture = command.GetTexID();
                    if (texture != 0 && texture != ImGui::GetIO().Fonts->TexID && command.ElemCount > 0 &&
                        std::find(images.begin(), images.end(), texture) == images.end())
                    {
                        images.push_back(texture);
                    }
                }
            }
            return images;
        }

        NS::Application m_app{PreviewAppDesc()};
        NS::AssetManager m_assets{NS::OS::FileSystem::ContentRoot()};
        NS::AssetManagerScope m_useAssets{m_assets};
        NS::UI::ImGuiContext m_gui{m_app.Window(), m_app.Device()};
        NS::Scene m_scene;
        LevelEditorController m_editor{&m_scene};
        NS::Editor::InspectorPanel m_inspector;
        NS::Editor::AssetsPanel m_assetsPanel;
    };
}

TEST_F(EditorAssetPreview, TextureSelectionDisplaysTheSelectedImage)
{
    m_editor.SelectAsset(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::ContentRoot(), "Assets/Textures/cube_test.png"));
    const NS::Texture* texture = NS::Cast<NS::Texture>(m_editor.SelectedAsset());
    ASSERT_NE(texture, nullptr);
    EXPECT_FALSE(m_editor.HasInspectableSelection());
    EXPECT_EQ(m_editor.SelectedAssetPath(), "Assets/Textures/cube_test.png");
    (void)Frame();
    const ImTextureID image = static_cast<ImTextureID>(reinterpret_cast<NS::uintptr>(texture->D3D()->Srv()));
    EXPECT_TRUE(ShowsImage(image));
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/asset-texture-preview.png"));
}

TEST_F(EditorAssetPreview, SkyboxSettingsPreviewAndUndoPreserveTheScene)
{
    NS::Editor::ObjectSnapshotApplier applier{&m_scene};
    m_editor.Editor().SetApplier(&applier);
    const nlohmann::json before = m_scene.ToJson();
    const std::vector<NS::Editor::AssetOption> options =
        NS::Editor::CollectSkyboxOptions(NS::OS::FileSystem::ContentRoot());
    ASSERT_TRUE(std::any_of(options.begin(), options.end(), [](const NS::Editor::AssetOption& option) {
        return option.value == "Assets/Skybox/kurt";
    }));
    EXPECT_FALSE(m_editor.SetSkyboxPath("../outside"));
    EXPECT_FALSE(m_editor.SetSkyboxPath("Assets/Textures/cube_test.png"));
    EXPECT_FALSE(m_editor.SetSkyboxPath("Assets/Skybox"));
    EXPECT_FALSE(m_editor.SetSkyboxPath("Assets/Skybox/missing"));
    EXPECT_EQ(m_scene.ToJson(), before);
    EXPECT_FALSE(m_editor.Editor().HasUnsavedChanges());
    ASSERT_TRUE(m_editor.SetSkyboxPath("Assets/Skybox/kurt"));
    EXPECT_TRUE(m_editor.Editor().HasUnsavedChanges());
    const nlohmann::json saved = m_scene.ToJson();
    EXPECT_EQ(NS::SceneJsonSkybox(saved), "Assets/Skybox/kurt");
    EXPECT_EQ(saved["objects"], before["objects"]);
    ASSERT_TRUE(NS::SaveSceneToJsonFile(saved, "build/skybox-inspector.scene"));
    nlohmann::json loaded;
    ASSERT_TRUE(NS::LoadSceneFromJsonFile(loaded, "build/skybox-inspector.scene"));
    NS::Scene restored;
    restored.ResetFromJson(std::move(loaded));
    EXPECT_EQ(restored.SkyboxPath(), "Assets/Skybox/kurt");
    const std::size_t operations = m_editor.Editor().Undo().UndoSize();
    EXPECT_TRUE(m_editor.SetSkyboxPath("Assets/Skybox/./kurt"));
    EXPECT_EQ(m_editor.Editor().Undo().UndoSize(), operations);

    m_editor.SelectAsset(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::ContentRoot(), "Assets/Textures/cube_test.png"));
    ASSERT_NE(m_editor.SelectedAsset(), nullptr);
    (void)Frame();
    const std::vector<ImTextureID> baseline = ShownImages();
    m_editor.SelectSceneSettings();
    EXPECT_EQ(m_editor.SelectedAsset(), nullptr);
    EXPECT_FALSE(m_editor.HasInspectableSelection());
    const std::vector<NS::uint8> pixels = Frame();
    bool preview = false;
    for (const ImTextureID image : ShownImages())
    {
        preview |= std::find(baseline.begin(), baseline.end(), image) == baseline.end();
    }
    EXPECT_TRUE(preview);
    EXPECT_FALSE(pixels.empty());
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/skybox-inspector-preview.png"));

    ASSERT_TRUE(m_editor.SetSkyboxPath(""));
    EXPECT_TRUE(m_scene.SkyboxPath().empty());
    ASSERT_TRUE(m_editor.Editor().PerformUndo());
    EXPECT_EQ(m_editor.SkyboxPath(), "Assets/Skybox/kurt");
    ASSERT_TRUE(m_editor.Editor().PerformUndo());
    EXPECT_EQ(m_scene.ToJson(), before);
    ASSERT_TRUE(m_editor.Editor().PerformRedo());
    EXPECT_EQ(m_editor.SkyboxPath(), "Assets/Skybox/kurt");
}

TEST_F(EditorAssetPreview, SkyboxPickerPreviewsBeforeApplyingAndCanHideTheBackground)
{
    NS::Editor::ObjectSnapshotApplier applier{&m_scene};
    m_editor.Editor().SetApplier(&applier);
    (void)Frame();
    Click(45.0f, 169.0f);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    ImGuiWindow* popup = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(popup, nullptr);
    const float rowY = popup->DC.CursorMaxPos.y - ImGui::GetTextLineHeight() * 0.5f;
    Click(popup->DC.CursorStartPos.x + 60.0f, rowY);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
    EXPECT_TRUE(m_scene.SkyboxPath().empty());
    EXPECT_FALSE(m_editor.Editor().HasUnsavedChanges());
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/skybox-inspector-before-apply.png"));
    Click(25.0f, 437.0f);
    ASSERT_EQ(m_scene.SkyboxPath(), "Assets/Skybox/kurt");
    Click(90.0f, 437.0f);
    EXPECT_TRUE(m_scene.SkyboxPath().empty());
    ASSERT_TRUE(m_editor.Editor().PerformUndo());
    EXPECT_EQ(m_scene.SkyboxPath(), "Assets/Skybox/kurt");
}

TEST_F(EditorAssetPreview, MaterialPreviewChangesWithBaseColor)
{
    m_editor.SelectAsset(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::ContentRoot(), "Assets/Materials/default.mat"));
    NS::Material* material = NS::Cast<NS::Material>(m_editor.SelectedAsset());
    ASSERT_NE(material, nullptr);
    material->SetBaseColor(NS::Color{1.0f, 0.0f, 0.0f, 1.0f});
    const std::vector<NS::uint8> red = Frame();
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/asset-material-preview.png"));
    material->SetBaseColor(NS::Color{0.0f, 0.0f, 1.0f, 1.0f});
    const std::vector<NS::uint8> blue = Frame();
    ASSERT_EQ(red.size(), blue.size());
    int changed = 0;
    for (std::size_t pixel = 0; pixel < red.size(); pixel += 4)
    {
        if (red[pixel + 2] > blue[pixel + 2] + 30 && blue[pixel] > red[pixel] + 30)
        {
            ++changed;
        }
    }
    EXPECT_GT(changed, 1500);
    material->SetTexturePath<0>("Assets/Textures/入力途中の名前.png");
    {
        const TestLogCapture logs;
        EXPECT_FALSE(Frame().empty());
        EXPECT_EQ(logs.ErrorCount(), 0);
    }
    m_assets.Clear();
    EXPECT_NE(m_editor.SelectedAsset(), nullptr);
    EXPECT_FALSE(Frame().empty());
}

TEST_F(EditorAssetPreview, ModelsOfDifferentSizesFitInThePreview)
{
    const char* models[] = {"Assets/Models/CesiumMan.glb", "Assets/Models/Xbot.glb"};
    for (const char* path : models)
    {
        m_editor.SelectAsset(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), path));
        ASSERT_NE(NS::Cast<NS::Mesh>(m_editor.SelectedAsset()), nullptr);
        const std::vector<NS::uint8> pixels = Frame();
        int visible = 0;
        int edge = 0;
        for (int y = 90; y < 290; ++y)
        {
            for (int x = 8; x < 260; ++x)
            {
                const std::size_t at = static_cast<std::size_t>(y * 500 + x) * 4;
                if (pixels[at] > 70 && pixels[at + 1] > 70 && pixels[at + 2] > 70)
                {
                    ++visible;
                    if (x < 12 || x > 255)
                    {
                        ++edge;
                    }
                }
            }
        }
        EXPECT_GT(visible, 500) << path;
        EXPECT_EQ(edge, 0) << path;
    }
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/asset-model-preview.png"));
    const std::vector<NS::uint8> before = Frame();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(130.0f, 180.0f);
    (void)Frame();
    io.AddMouseButtonEvent(0, true);
    (void)Frame();
    io.AddMousePosEvent(185.0f, 195.0f);
    const std::vector<NS::uint8> rotated = Frame();
    io.AddMouseButtonEvent(0, false);
    (void)Frame();
    EXPECT_NE(before, rotated);
    io.AddMouseWheelEvent(0.0f, 1.0f);
    const std::vector<NS::uint8> zoomed = Frame();
    EXPECT_NE(rotated, zoomed);
}

TEST_F(EditorAssetPreview, AssetsShowsMaterialsModelsAndTexturesBeforeSelection)
{
    (void)Frame(true);
    const std::vector<ImTextureID> before = ShownImages();
    const auto thumbnailCount = [&] {
        int count = 0;
        for (const ImTextureID image : ShownImages())
        {
            if (std::find(before.begin(), before.end(), image) == before.end())
            {
                ++count;
            }
        }
        return count;
    };
    m_assets.LoadMaterial("Assets/Materials/default.mat")->SetBaseColor(NS::Color{1.0f, 0.0f, 0.0f, 1.0f});
    m_assets.LoadMaterial("Assets/Materials/flat.mat")->SetBaseColor(NS::Color{0.0f, 0.0f, 1.0f, 1.0f});
    OpenAssetFolder("Materials");
    EXPECT_GE(thumbnailCount(), 2);
    const std::vector<NS::uint8> materials = Frame(true);
    int red = 0;
    int blue = 0;
    for (std::size_t pixel = 0; pixel < materials.size(); pixel += 4)
    {
        if (materials[pixel + 2] > materials[pixel] + 30)
        {
            ++red;
        }
        if (materials[pixel] > materials[pixel + 2] + 30)
        {
            ++blue;
        }
    }
    EXPECT_GT(red, 100);
    EXPECT_GT(blue, 100);
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/assets-material-thumbnails.png"));
    OpenAssetFolder("Models");
    EXPECT_EQ(thumbnailCount(), 3);
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/assets-model-thumbnails.png"));
    OpenAssetFolder("Textures");
    const NS::Texture* texture = m_assets.LoadTexture(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::ContentRoot(), "Assets/Textures/cube_test.png"));
    const ImTextureID image = static_cast<ImTextureID>(reinterpret_cast<NS::uintptr>(texture->D3D()->Srv()));
    EXPECT_TRUE(ShowsImage(image));
    EXPECT_TRUE(m_app.Device().CaptureBackbufferToPng("build/assets-texture-thumbnails.png"));
    EXPECT_TRUE(m_editor.SelectedAssetPath().empty());
    ImVec2 center{};
    bool found = false;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int list = 0; list < data->CmdListsCount; ++list)
    {
        const ImDrawList* draws = data->CmdLists[list];
        for (const ImDrawCmd& command : draws->CmdBuffer)
        {
            if (command.GetTexID() == image && command.ElemCount >= 3)
            {
                const ImVec2 first = draws->VtxBuffer[command.VtxOffset + draws->IdxBuffer[command.IdxOffset]].pos;
                const ImVec2 third = draws->VtxBuffer[command.VtxOffset + draws->IdxBuffer[command.IdxOffset + 2]].pos;
                center = ImVec2{(first.x + third.x) * 0.5f, (first.y + third.y) * 0.5f};
                found = true;
            }
        }
    }
    ASSERT_TRUE(found);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x, center.y);
    (void)Frame(true);
    io.AddMouseButtonEvent(0, true);
    (void)Frame(true);
    io.AddMouseButtonEvent(0, false);
    (void)Frame(true);
    EXPECT_EQ(m_editor.SelectedAssetPath(), "Assets/Textures/cube_test.png");
}
