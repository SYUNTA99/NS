#include "Editor/AssetPicker.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <span>

namespace
{
    class EditorAssetPickerTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_gui = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{1100.0f, 850.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigInputTrickleEventQueue = false;
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            m_model.SetMeshRef("sphere");
            for (int i = 0; i < 3; ++i)
            {
                (void)Frame();
            }
        }

        void TearDown() override { ImGui::DestroyContext(m_gui); }

        NS::Editor::ClassObjEditResult Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
            ImGui::SetNextWindowSize(ImVec2{740.0f, 650.0f});
            ImGui::Begin("素材の欄");
            m_tableMin = ImGui::GetCursorScreenPos();
            m_tableWidth = ImGui::GetContentRegionAvail().x;
            const NS::Editor::ClassObjEditResult result =
                NS::Editor::DrawReflectedClassObj(m_model, {}, nullptr, "メッシュ");
            ImGui::End();
            ImGui::EndFrame();
            return result;
        }

        void OpenPicker()
        {
            const ImVec2 at{m_tableMin.x + m_tableWidth * 0.65f,
                            m_tableMin.y + ImGui::GetStyle().CellPadding.y + ImGui::GetFrameHeight() * 0.5f};
            ImGui::GetIO().AddMousePosEvent(at.x, at.y);
            (void)Frame();
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            m_activated = Frame().activated;
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            const NS::Editor::ClassObjEditResult opened = Frame();
            m_activated |= opened.activated;
            m_committed = opened.committed;
            (void)Frame();
        }

        ImGuiContext* m_gui = nullptr;
        NS::Model m_model{nullptr};
        ImVec2 m_tableMin{};
        float m_tableWidth = 0.0f;
        bool m_activated = false;
        bool m_committed = false;
    };

    class AssetPickerCatalogTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_root = std::filesystem::temp_directory_path() /
                     ("ns_asset_picker_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ASSERT_TRUE(NS::OS::FileSystem::CreateDirectories(m_root.generic_string()));
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_root, error);
        }

        void File(const char* relative)
        {
            const std::string contents = "{}";
            ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes((m_root / relative).generic_string(),
                        std::as_bytes(std::span<const char>{contents.data(), contents.size()})));
        }

        std::filesystem::path m_root;
    };

    struct StringSettings
    {
        std::string text;
        NS_REFLECT_BEGIN(StringSettings, void)
        NS_FIELD(text, "説明")
        NS_REFLECT_END_VALUE()
    };

    struct EffectSettings
    {
        NS::EffectName effect{"charge.curl"};
        NS_REFLECT_BEGIN(EffectSettings, void)
        NS_FIELD(effect, "エフェクト")
        NS_REFLECT_END_VALUE()
    };
}

TEST(AssetPickerCatalog, EffectNameMemberIsAnEffectFieldSavedAsItsName)
{
    const NS::ReflectionInfo& info = *EffectSettings::StaticReflection();
    const NS::FieldDesc* field = NS::FindField(&info, "エフェクト");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->assetKind, NS::AssetKind::Effect);

    EffectSettings settings;
    EXPECT_EQ(NS::SerializeReflectedFields(&settings, info)["エフェクト"], nlohmann::json("charge.curl"));
    EXPECT_EQ(NS::ApplyReflectedFields(&settings, info, nlohmann::json{{"エフェクト", "impact.ring"}}), 0u);
    EXPECT_EQ(settings.effect.name, "impact.ring");
}

TEST_F(EditorAssetPickerTest, FileReferenceOpensChoicesBeforeChangingTheValue)
{
    const nlohmann::json before = NS::SerializeClassObjFields(m_model);
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    EXPECT_TRUE(m_activated);
    EXPECT_FALSE(m_committed);
    EXPECT_EQ(NS::SerializeClassObjFields(m_model), before);
}

TEST_F(EditorAssetPickerTest, SearchFiltersChoicesAndSelectionWritesOnlyTheSelectedName)
{
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    ImGui::GetIO().AddInputCharactersUTF8("capsule");
    EXPECT_FALSE(Frame().changed);
    (void)Frame();
    EXPECT_EQ(m_model.MeshRef(), "sphere");
    ImGuiWindow* choices = nullptr;
    for (ImGuiWindow* window : GImGui->Windows)
    {
        if (window->Active && (window->Flags & ImGuiWindowFlags_ChildWindow) != 0)
        {
            choices = window;
        }
    }
    ASSERT_NE(choices, nullptr);
    const ImVec2 row{choices->DC.CursorStartPos.x + 60.0f,
                     choices->DC.CursorStartPos.y + ImGui::GetTextLineHeight() * 0.5f};
    ImGui::GetIO().AddMousePosEvent(row.x, row.y);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    const NS::Editor::ClassObjEditResult selected = Frame();
    EXPECT_TRUE(selected.changed);
    EXPECT_EQ(m_model.MeshRef(), "capsule");
    EXPECT_EQ(selected.changedTarget, &m_model);
    EXPECT_TRUE(selected.committed || Frame().committed);
}

TEST_F(EditorAssetPickerTest, OpeningAndClosingPreservesAnUnlistedLongReference)
{
    const std::string path = "Assets/Models/" + std::string(350, 'a') + "/missing.glb";
    const TestLogCapture log;
    m_model.SetMeshRef(path);
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    ImGui::GetIO().AddMousePosEvent(1050.0f, 820.0f);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    bool committed = Frame().committed;
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    committed |= Frame().committed;
    EXPECT_TRUE(committed);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
    EXPECT_EQ(m_model.MeshRef(), path);
    EXPECT_EQ(log.ErrorCount(), 2);
}

TEST_F(AssetPickerCatalogTest, ChoicesIncludeBuiltinsAndOnlyFilesOfTheRequestedKind)
{
    File("Assets/Models/folder/test.GLB");
    File("Assets/Materials/folder/test.mat");
    File("Assets/Models/readme.txt");
    const std::vector<NS::Editor::AssetOption> meshes = NS::Editor::CollectAssetOptions(NS::AssetKind::Mesh, m_root.generic_string());
    bool builtin = false;
    bool model = false;
    for (const NS::Editor::AssetOption& option : meshes)
    {
        builtin |= option.value == "capsule";
        model |= option.value == "Assets/Models/folder/test.GLB";
        EXPECT_NE(option.value, "Assets/Materials/folder/test.mat");
        EXPECT_NE(option.value, "Assets/Models/readme.txt");
    }
    EXPECT_TRUE(builtin);
    EXPECT_TRUE(model);
    const std::vector<NS::Editor::AssetOption> materials = NS::Editor::CollectAssetOptions(NS::AssetKind::Material, m_root.generic_string());
    EXPECT_TRUE(std::any_of(materials.begin(), materials.end(), [](const NS::Editor::AssetOption& option) {
        return option.value == "Assets/Materials/folder/test.mat" && option.label == "test.mat" &&
               option.location == "Assets/Materials/folder";
    }));
    const std::vector<NS::Editor::AssetOption> models = NS::Editor::CollectAssetOptions(NS::AssetKind::Model, m_root.generic_string());
    ASSERT_EQ(models.size(), 1);
    EXPECT_EQ(models[0].value, "Assets/Models/folder/test.GLB");
}

TEST_F(AssetPickerCatalogTest, EffectChoicesKeepTheNamesExpectedByTheEffectLoader)
{
    File("Assets/Effects/charge.curl.efkefc");
    File("Assets/Effects/group/impact.efkefc");
    File("Assets/Effects/Texture/spark.png");
    const std::vector<NS::Editor::AssetOption> options = NS::Editor::CollectAssetOptions(NS::AssetKind::Effect, m_root.generic_string());
    ASSERT_EQ(options.size(), 2);
    EXPECT_EQ(options[0].value, "charge.curl");
    EXPECT_EQ(options[1].value, "group/impact");
}

TEST_F(AssetPickerCatalogTest, SkyboxesRequireSixFacesOrACubemapDds)
{
    const char* faces[] = {"space_ft.png", "space_bk.png", "space_up.png",
                           "space_dn.png", "space_rt.png", "space_lf.png"};
    for (const char* face : faces)
    {
        const std::string relative = "Assets/Skybox/complete/" + std::string{face};
        File(relative.c_str());
    }
    File("Assets/Skybox/incomplete/space_ft.png");
    File("Assets/Skybox/invalid.dds");
    std::vector<std::byte> bytes(152);
    const auto word = [&](std::size_t offset, NS::uint32 value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    word(0, 0x20534444);
    word(4, 124);
    word(12, 1);
    word(16, 1);
    word(76, 32);
    const std::string flat = (m_root / "Assets/Skybox/flat.dds").generic_string();
    ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(flat, bytes));
    EXPECT_FALSE(NS::Editor::IsSkyboxAssetPath(flat));
    word(112, 0xfe00);
    const std::string cube = (m_root / "Assets/Skybox/cube.DDS").generic_string();
    ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(cube, bytes));
    EXPECT_TRUE(NS::Editor::IsSkyboxAssetPath(cube));
    word(84, 0x30315844);
    word(132, 3);
    word(136, 4);
    word(140, 1);
    const std::string dx10 = (m_root / "Assets/Skybox/cube10.dds").generic_string();
    ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(dx10, bytes));
    EXPECT_TRUE(NS::Editor::IsSkyboxAssetPath(dx10));
    word(140, 2);
    const std::string array = (m_root / "Assets/Skybox/array.dds").generic_string();
    ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(array, bytes));
    EXPECT_FALSE(NS::Editor::IsSkyboxAssetPath(array));
    const std::vector<NS::Editor::AssetOption> options =
        NS::Editor::CollectSkyboxOptions(m_root.generic_string());
    ASSERT_EQ(options.size(), 3);
    EXPECT_TRUE(std::any_of(options.begin(), options.end(), [](const NS::Editor::AssetOption& option) {
        return option.value == "Assets/Skybox/complete";
    }));
}

TEST(AssetPickerCatalog, AnimationChoicesIncludeTheNamedClipsInsideFiles)
{
    const std::vector<NS::Editor::AssetOption> options =
        NS::Editor::CollectAssetOptions(NS::AssetKind::Animation, NS::OS::FileSystem::ContentRoot());
    EXPECT_TRUE(std::any_of(options.begin(), options.end(), [](const NS::Editor::AssetOption& option) {
        return option.value == "Assets/Models/Soldier.glb#Walk" && option.label == "Walk";
    }));
    EXPECT_TRUE(std::any_of(options.begin(), options.end(), [](const NS::Editor::AssetOption& option) {
        return option.value == "Assets/Models/Soldier.glb#Run" && option.label == "Run";
    }));
    EXPECT_TRUE(std::any_of(options.begin(), options.end(), [](const NS::Editor::AssetOption& option) {
        return option.value == "Assets/Models/CesiumMan.glb#" && !option.label.empty();
    }));
}

TEST_F(EditorAssetPickerTest, PlainStringsCanBeEditedBeyondTheOldBufferLimit)
{
    StringSettings settings;
    settings.text = std::string(350, 'a');
    const auto frame = [&](bool focus) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{740.0f, 400.0f});
        ImGui::Begin("文字列の欄");
        if (focus)
        {
            ImGui::SetKeyboardFocusHere();
        }
        const NS::Editor::ValueEditResult result = NS::Editor::DrawReflectedValue(settings);
        ImGui::End();
        ImGui::EndFrame();
        return result;
    };
    (void)frame(true);
    (void)frame(false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_End, true);
    (void)frame(false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_End, false);
    ImGui::GetIO().AddInputCharactersUTF8("追加");
    EXPECT_TRUE(frame(false).changed);
    EXPECT_EQ(settings.text, std::string(350, 'a') + "追加");
}
