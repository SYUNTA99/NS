#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <map>
#include <optional>
#include <string>

namespace
{
    class EditorColorTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_gui = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{1000.0f, 800.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigInputTrickleEventQueue = false;
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        void TearDown() override { ImGui::DestroyContext(m_gui); }

        NS::Editor::ClassObjEditResult Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
            ImGui::SetNextWindowSize(ImVec2{700.0f, 600.0f});
            ImGui::Begin("色の欄");
            m_tableMin = ImGui::GetCursorScreenPos();
            m_tableWidth = ImGui::GetContentRegionAvail().x;
            const NS::Editor::ClassObjEditResult result =
                NS::Editor::DrawReflectedClassObj(m_model, {}, nullptr, "色の上書き");
            ImGui::End();
            ImGui::EndFrame();
            return result;
        }

        void OpenPicker(bool fromLeft = false)
        {
            for (int i = 0; i < 3; ++i)
            {
                (void)Frame();
            }
            const ImGuiStyle& style = ImGui::GetStyle();
            float x = m_tableMin.x + m_tableWidth - style.CellPadding.x -
                      NS::Editor::RevertButtonWidth() - style.ItemSpacing.x - ImGui::GetFrameHeight() * 0.5f;
            if (fromLeft)
            {
                x = m_tableMin.x + m_tableWidth * 0.5f;
            }
            const float y = m_tableMin.y + style.CellPadding.y + ImGui::GetFrameHeight() * 0.5f;
            ImGui::GetIO().AddMousePosEvent(x, y);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            m_openActivated = Frame().activated;
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            const NS::Editor::ClassObjEditResult result = Frame();
            m_openActivated |= result.activated;
            m_openCommitted = result.committed;
        }

        ImGuiContext* m_gui = nullptr;
        NS::Model m_model{nullptr};
        ImVec2 m_tableMin{};
        float m_tableWidth = 0.0f;
        bool m_openActivated = false;
        bool m_openCommitted = false;
    };

    struct ColorSettings
    {
        NS::Color tint{0.1f, 0.2f, 0.3f, 0.4f};
        NS_REFLECT_BEGIN(ColorSettings, void)
        NS_FIELD(tint, "色合い")
        NS_REFLECT_END_VALUE()
    };

    // 保存された "基本色" の rgb。無いか、上書きの無い null なら空
    std::optional<NS::Vector3> SavedModelColor(const nlohmann::json& object)
    {
        const nlohmann::json::const_iterator classObjs = object.find("classObjs");
        if (classObjs == object.end() || !classObjs->contains("Model") || !(*classObjs)["Model"].contains("基本色"))
        {
            return std::nullopt;
        }
        const nlohmann::json& color = (*classObjs)["Model"]["基本色"];
        if (!color.is_array())
        {
            return std::nullopt;
        }
        return NS::Vector3{color[0].get<float>(), color[1].get<float>(), color[2].get<float>()};
    }

    // 色の正をマテリアルへ移す前に描いていた色。配置物の "基本色"、.actor の "基本色"、
    // クラスのコードが入れる色、Model の既定の白の順に引く
    NS::Vector3 ColorBeforeTheMaterialOwnedIt(const nlohmann::json& object, const std::string& actorFileName)
    {
        const std::optional<NS::Vector3> placed = SavedModelColor(object);
        if (placed)
        {
            return *placed;
        }
        const std::string actorFilePath =
            NS::OS::FileSystem::Combine(NS::ActorFileLibrary::Get().Directory(), actorFileName + ".actor");
        if (NS::OS::FileSystem::Exists(actorFilePath))
        {
            const std::optional<std::string> actorFileText = NS::OS::FileSystem::ReadAllText(actorFilePath);
            EXPECT_TRUE(actorFileText.has_value()) << actorFilePath;
            if (!actorFileText)
            {
                return NS::Vector3{};
            }
            const std::optional<NS::Vector3> actorFile = SavedModelColor(nlohmann::json::parse(*actorFileText));
            if (actorFile)
            {
                return *actorFile;
            }
        }
        const std::string className = NS::ActorFileLibrary::Get().ClassNameOf(actorFileName);
        const std::map<std::string, NS::Vector3> classColors{
            {"GoalActor", NS::Vector3{1.0f, 0.84f, 0.0f}},
            {"PropActor", NS::Vector3{0.72f, 0.70f, 0.66f}},
            {"PlayerActor", NS::Vector3{0.5f, 0.5f, 0.5f}},
            {"MapPartsActor", NS::Vector3{0.70f, 0.70f, 0.75f}},
        };
        const std::map<std::string, NS::Vector3>::const_iterator found = classColors.find(className);
        if (found != classColors.end())
        {
            return found->second;
        }
        return NS::Vector3{1.0f, 1.0f, 1.0f};
    }
}

// 上書きが空なら描くマテリアルの基本色、入っていれば上書きの色で描く。掛け合わせない
TEST(ModelDrawColor, EmptyOverrideDrawsTheMaterialColorAndAnOverrideWins)
{
    NS::Model model{nullptr};
    EXPECT_FALSE(model.ColorOverride().has_value());
    model.SetMaterialRef("Assets/Materials/stone.mat");
    EXPECT_EQ(model.DrawColor().ToVector3(), (NS::Vector3{0.6f, 0.6f, 0.65f}));

    model.SetColorOverride(NS::Color{0.2f, 0.4f, 0.7f});
    EXPECT_EQ(model.DrawColor().ToVector3(), (NS::Vector3{0.2f, 0.4f, 0.7f}));

    model.SetColorOverride(std::nullopt);
    EXPECT_EQ(model.DrawColor().ToVector3(), (NS::Vector3{0.6f, 0.6f, 0.65f}));
}

// 出荷のシーンと .actor から作ったアクターは、色の正を移す前と同じ色で描く
TEST(ModelDrawColor, ShippedScenesAndActorFilesKeepTheirColor)
{
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    int checked = 0;

    for (const std::string& name : library.Names())
    {
        const NS::Model* model = library.Baseline(name).ModelClassObj();
        if (model == nullptr)
        {
            continue;
        }
        EXPECT_EQ(model->DrawColor().ToVector3(), ColorBeforeTheMaterialOwnedIt(nlohmann::json::object(), name))
            << name;
        ++checked;
    }

    const std::string scenes = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Scenes");
    for (const std::string& path : NS::OS::FileSystem::ListFiles(scenes, ".scene"))
    {
        const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(path);
        ASSERT_TRUE(text.has_value()) << path;
        nlohmann::json doc;
        ASSERT_TRUE(NS::DeserializeSceneFromJson(doc, *text)) << path;
        NS::Scene scene;
        scene.ResetFromJson(doc);
        for (const nlohmann::json& object : NS::SceneJsonObjects(doc))
        {
            const NS::ObjectHeader header = NS::ReadObjectHeader(object);
            const NS::SceneActor* actor = scene.Objects().FindByObjectId(header.id);
            ASSERT_NE(actor, nullptr) << path << " " << header.id;
            if (actor->ModelClassObj() == nullptr)
            {
                continue;
            }
            EXPECT_EQ(actor->ModelClassObj()->DrawColor().ToVector3(),
                      ColorBeforeTheMaterialOwnedIt(object, header.actorFile))
                << path << " " << header.name;
            ++checked;
        }
    }
    EXPECT_GT(checked, 0);
}

TEST(ColorField, ColorMemberIsAColorFieldSavedWithItsAlpha)
{
    const NS::ReflectionInfo& info = *ColorSettings::StaticReflection();
    const NS::FieldDesc* field = NS::FindField(&info, "色合い");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::Color);

    const ColorSettings settings;
    const nlohmann::json saved = NS::SerializeReflectedFields(&settings, info);
    EXPECT_EQ(saved["色合い"], nlohmann::json::array({0.1f, 0.2f, 0.3f, 0.4f}));
    ColorSettings loaded;
    loaded.tint = NS::Color{1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(NS::ApplyReflectedFields(&loaded, info, saved), 0u);
    EXPECT_TRUE(loaded.tint == settings.tint);
}

// 透明度の無い 3 成分は前の形の色。黙って既定に戻さず、警告して今の値を残す
TEST(ColorField, ThreeComponentColorWarnsAndKeepsTheValue)
{
    const NS::ReflectionInfo& info = *ColorSettings::StaticReflection();
    ColorSettings settings;
    const TestLogCapture log;
    (void)NS::ApplyReflectedFields(&settings, info, nlohmann::json{{"色合い", {1.0f, 1.0f, 1.0f}}});
    EXPECT_EQ(log.WarningCount(), 1);
    EXPECT_TRUE(settings.tint == (NS::Color{0.1f, 0.2f, 0.3f, 0.4f}));
}

TEST_F(EditorColorTest, ColorSwatchOpensAPickerBeforeChangingTheValue)
{
    m_model.SetColorOverride(NS::Color{0.2f, 0.4f, 0.7f});
    const nlohmann::json before = NS::SerializeClassObjFields(m_model);
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    EXPECT_TRUE(m_openActivated);
    EXPECT_FALSE(m_openCommitted);
    EXPECT_EQ(NS::SerializeClassObjFields(m_model), before);
}

TEST_F(EditorColorTest, ClickingTheLeftSideOfTheSwatchOpensThePicker)
{
    m_model.SetColorOverride(NS::Color{0.2f, 0.4f, 0.7f});
    const nlohmann::json before = NS::SerializeClassObjFields(m_model);
    OpenPicker(true);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 1);
    EXPECT_TRUE(m_openActivated);
    EXPECT_FALSE(m_openCommitted);
    EXPECT_EQ(NS::SerializeClassObjFields(m_model), before);
}

TEST_F(EditorColorTest, DrawingAColorFieldPreservesHighIntensityAndSavedNumbers)
{
    m_model.SetColorOverride(NS::Color{2.5f, 0.3f, 0.1f});
    const nlohmann::json before = NS::SerializeClassObjFields(m_model);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FALSE(Frame().changed);
    }
    EXPECT_EQ(NS::SerializeClassObjFields(m_model), before);
    ASSERT_TRUE(before.contains("基本色"));
    EXPECT_FLOAT_EQ(before["基本色"][0].get<float>(), 2.5f);
}

TEST_F(EditorColorTest, ChoosingAColorWritesTheFieldAndCommitsWhenThePickerCloses)
{
    m_model.SetColorOverride(NS::Color{0.2f, 0.4f, 0.7f});
    const nlohmann::json before = NS::SerializeClassObjFields(m_model);
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    (void)Frame();
    const ImGuiWindow* picker = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(picker, nullptr);
    const ImVec2 point{picker->DC.CursorStartPos.x + 40.0f, picker->DC.CursorStartPos.y + 40.0f};
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    const NS::Editor::ClassObjEditResult press = Frame();
    EXPECT_TRUE(press.changed);
    EXPECT_FALSE(press.committed);
    EXPECT_EQ(press.changedTarget, &m_model);
    ASSERT_NE(press.changedField, nullptr);
    EXPECT_STREQ(press.changedField->name, "基本色");
    ImGui::GetIO().AddMousePosEvent(point.x + 25.0f, point.y + 15.0f);
    EXPECT_FALSE(Frame().committed);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_FALSE(Frame().committed);
    EXPECT_NE(NS::SerializeClassObjFields(m_model), before);
    ImGui::GetIO().AddMousePosEvent(950.0f, 750.0f);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    bool committed = Frame().committed;
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    committed |= Frame().committed;
    committed |= Frame().committed;
    EXPECT_TRUE(committed);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
}
