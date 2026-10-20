#include "Editor/InspectorPanel.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "NSlib/Windows/Filesystem.h"
#include "Tests/TestArchetypeDirectory.h"
#include "Tests/TestCheckLayer.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// コンボで参照先を選ぶと、値が変わる前に編集の始まりを、変わった後に確定を返す
// 始まりが値の後だと、控えが変わった後の値になり undo に積まれない
TEST(EditorInspector, ComboSelectionStartsBeforeTheChangeAndCommitsAfter)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::ThirdPersonFollow follow{nullptr};
    const std::vector<NS::Editor::ObjectRefOption> options{{7u, "相手"}};
    ImVec2 tableMin{};
    float tableWidth = 0.0f;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{600.0f, 400.0f});
        ImGui::Begin("参照の欄");
        tableMin = ImGui::GetCursorScreenPos();
        tableWidth = ImGui::GetContentRegionAvail().x;
        const NS::Editor::ClassObjEditResult result =
            NS::Editor::DrawReflectedClassObj(follow, options, nullptr, "追従対象");
        ImGui::End();
        ImGui::EndFrame();
        return result;
    };
    const auto click = [&](ImVec2 at) {
        io.AddMousePosEvent(at.x, at.y);
        io.AddMouseButtonEvent(0, true);
        std::vector<NS::Editor::ClassObjEditResult> results{frame()};
        io.AddMouseButtonEvent(0, false);
        results.push_back(frame());
        return results;
    };

    // 検索で欄を 1 つに絞ったので、コンボは表の 1 行目の値の列にある。列の幅は数回描くと定まる
    for (int i = 0; i < 3; ++i)
    {
        (void)frame();
    }
    const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    (void)click(ImVec2{tableMin.x + tableWidth * 0.6f, tableMin.y + rowHeight * 0.5f});
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    (void)frame();

    const ImGuiWindow* popup = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(popup, nullptr);
    const float line = ImGui::GetTextLineHeight();
    const ImVec2 option{popup->Pos.x + popup->Size.x * 0.5f,
                        popup->Pos.y + popup->WindowPadding.y + line + ImGui::GetStyle().ItemSpacing.y + line * 0.5f};
    const std::vector<NS::Editor::ClassObjEditResult> results = click(option);
    ASSERT_EQ(follow.TargetRef().id, 7u);

    int activatedAt = -1;
    int changedAt = -1;
    int committedAt = -1;
    for (int i = 0; i < static_cast<int>(results.size()); ++i)
    {
        if (results[i].activated && activatedAt < 0)
        {
            activatedAt = i;
        }
        if (results[i].changed && changedAt < 0)
        {
            changedAt = i;
        }
        if (results[i].committed)
        {
            committedAt = i;
        }
    }
    EXPECT_GE(activatedAt, 0);
    EXPECT_LT(activatedAt, changedAt);
    EXPECT_GE(committedAt, changedAt);
    ImGui::DestroyContext(gui);
}

namespace
{
    class LabeledFieldObject final : public NS::Object
    {
    public:
        float speed = 2.0f;

        NS_REFLECT_BEGIN(LabeledFieldObject, NS::Object)
        NS_FIELD(speed, "speed", "走る速さ")
        NS_REFLECT_END()
    };
} // namespace

// 見せる名前を足した欄は、データの名前で保存されて読み戻され、インスペクタの見出しは見せる名前になる
TEST(EditorInspector, LabeledFieldSavesByNameAndShowsTheLabel)
{
    LabeledFieldObject object;
    const NS::ReflectionInfo& info = *object.GetReflection();
    EXPECT_EQ(NS::SerializeReflectedFields(&object, info), (nlohmann::json{{"speed", 2.0f}}));
    EXPECT_EQ(NS::ApplyReflectedFields(&object, info, nlohmann::json{{"speed", 5.0f}}), 0u);
    EXPECT_FLOAT_EQ(object.speed, 5.0f);

    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // 表の列の幅は数回描くと定まるので、最後のフレームの文字だけを写し取る
    std::string drawnText;
    for (int frame = 0; frame < 3; ++frame)
    {
        ImGui::NewFrame();
        ImGui::Begin("見せる名前の欄");
        const bool capture = frame == 2;
        if (capture)
        {
            ImGui::LogToBuffer();
        }
        (void)NS::Editor::DrawReflectedClassObj(object);
        if (capture)
        {
            drawnText = GImGui->LogBuffer.c_str();
            ImGui::LogFinish();
        }
        ImGui::End();
        ImGui::EndFrame();
    }
    ImGui::DestroyContext(gui);

    EXPECT_NE(drawnText.find("走る速さ"), std::string::npos) << drawnText;
    EXPECT_EQ(drawnText.find("speed"), std::string::npos) << drawnText;
}

namespace
{
    class VectorFieldObject final : public NS::Object
    {
    public:
        NS::Vector3 direction{1.0f, 2.0f, 3.0f};

        NS_REFLECT_BEGIN(VectorFieldObject, NS::Object)
        NS_FIELD(direction, "direction")
        NS_REFLECT_END()
    };
} // namespace

TEST(EditorInspector, VectorAxesStayReadableWhenThePanelNarrows)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{1000.0f, 800.0f};
    io.DeltaTime = 1.0f / 60.0f;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    VectorFieldObject object;
    const nlohmann::json before = NS::SerializeClassObjFields(object);
    const float widths[] = {760.0f, 360.0f, 170.0f};
    float heights[3]{};
    for (int size = 0; size < 3; ++size)
    {
        SCOPED_TRACE(widths[size]);
        for (int frame = 0; frame < 3; ++frame)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
            ImGui::SetNextWindowSize(ImVec2{widths[size], 400.0f});
            ImGui::Begin("Vector fields", nullptr, ImGuiWindowFlags_NoSavedSettings);
            const float top = ImGui::GetCursorScreenPos().y;
            ImGui::LogToBuffer();
            const NS::Editor::ClassObjEditResult edit = NS::Editor::DrawReflectedClassObj(object);
            const std::string text = GImGui->LogBuffer.c_str();
            ImGui::LogFinish();
            EXPECT_FALSE(edit.changed);
            heights[size] = ImGui::GetCursorScreenPos().y - top;
            if (frame == 2)
            {
                EXPECT_NE(text.find("X"), std::string::npos) << text;
                EXPECT_NE(text.find("Y"), std::string::npos) << text;
                EXPECT_NE(text.find("Z"), std::string::npos) << text;
                const ImGuiTable* table = ImGui::TableFindByID(ImGui::GetID("##fields"));
                ASSERT_NE(table, nullptr);
                int expectedColumns = 1;
                if (size == 0)
                {
                    expectedColumns = 2;
                }
                EXPECT_EQ(table->ColumnsCount, expectedColumns);
                EXPECT_LE(ImGui::GetCurrentWindow()->DC.CursorMaxPos.x,
                          ImGui::GetCurrentWindow()->WorkRect.Max.x + 1.0f);
            }
            ImGui::End();
            ImGui::EndFrame();
        }
    }
    EXPECT_GT(heights[1], heights[0]);
    EXPECT_GT(heights[2], heights[1]);
    EXPECT_EQ(NS::SerializeClassObjFields(object), before);
    ImGui::DestroyContext(gui);
}

namespace
{
    class TwoAxisFieldObject final : public NS::Object
    {
    public:
        NS::Vector2 offset{0.25f, 0.75f};

        NS_REFLECT_BEGIN(TwoAxisFieldObject, NS::Object)
        NS_FIELD(offset, "offset")
        NS_REFLECT_END()
    };
} // namespace

TEST(EditorInspector, Vector2SavesBothComponentsRejectsBrokenValuesAndReverts)
{
    TwoAxisFieldObject object;
    const TwoAxisFieldObject defaults;
    const NS::FieldDesc* field = NS::FindField(object.GetReflection(), "offset");
    ASSERT_NE(field, nullptr);
    const nlohmann::json saved = NS::SerializeClassObjFields(object);
    EXPECT_EQ(saved, (nlohmann::json{{"offset", {0.25f, 0.75f}}}));
    object.offset = NS::Vector2{4.0f, 5.0f};
    EXPECT_TRUE(NS::Editor::FieldDiffersFromDefault(object, &defaults, *field));
    NS::Editor::RevertFieldToDefault(object, defaults, *field);
    EXPECT_EQ(object.offset, defaults.offset);
    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(object, &defaults, *field));
    object.offset = NS::Vector2{4.0f, 5.0f};
    EXPECT_EQ(NS::ApplyJsonFields(object, saved), 0u);
    EXPECT_EQ(object.offset, defaults.offset);
    const nlohmann::json broken[] = {
        nullptr, 3, {1.0f}, {1.0f, 2.0f, 3.0f}, {1.0f, "bad"}, {1.0f, std::numeric_limits<double>::max()}};
    for (const nlohmann::json& value : broken)
    {
        (void)NS::ApplyJsonFields(object, nlohmann::json{{"offset", value}});
        EXPECT_EQ(object.offset, defaults.offset);
    }
}

TEST(EditorInspector, Vector2ValueAxesEditIndependentlyAcrossPanelResizes)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{1000.0f, 800.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    TwoAxisFieldObject object;
    float panelWidth = 760.0f;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{panelWidth, 400.0f});
        ImGui::Begin("Vector2 values", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const NS::Editor::ValueEditResult result = NS::Editor::DrawReflectedValue(&object, *object.GetReflection());
        ImGui::End();
        ImGui::EndFrame();
        return result;
    };
    (void)frame();
    (void)frame();
    const char* axes[] = {"X", "Y"};
    const char* values[] = {"12.5", "-3.25"};
    for (int axis = 0; axis < 2; ++axis)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Vector2 values");
        ImGuiID inputId = ImHashStr("##value-fields", 0, window->ID);
        const int fieldIndex = 0;
        inputId = ImHashData(&fieldIndex, sizeof(fieldIndex), inputId);
        inputId = ImHashStr(axes[axis], 0, inputId);
        inputId = ImHashStr("##value", 0, inputId);
        ImGui::ActivateItemByID(inputId);
        GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        EXPECT_TRUE(frame().activated);
        EXPECT_EQ(GImGui->ActiveId, inputId);
        panelWidth = 170.0f;
        EXPECT_FALSE(frame().committed);
        io.AddInputCharactersUTF8(values[axis]);
        const NS::Editor::ValueEditResult edit = frame();
        EXPECT_TRUE(edit.changed);
        ASSERT_NE(edit.changedField, nullptr);
        EXPECT_STREQ(edit.changedField->name, "offset");
        if (axis == 0)
        {
            EXPECT_FLOAT_EQ(object.offset.y, 0.75f);
        }
        else
        {
            EXPECT_FLOAT_EQ(object.offset.x, 12.5f);
        }
        io.AddKeyEvent(ImGuiKey_Enter, true);
        EXPECT_TRUE(frame().committed);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        panelWidth = 760.0f;
        (void)frame();
    }
    EXPECT_EQ(object.offset, (NS::Vector2{12.5f, -3.25f}));
    ImGui::DestroyContext(gui);
}

namespace
{
    // インスペクタを、欄の行も上書きの印の選びも全部入る大きさで描く
    class InspectorFrames
    {
    public:
        explicit InspectorFrames(LevelEditorController& editor) : m_editor(editor)
        {
            m_gui = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{800.0f, 2000.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigInputTrickleEventQueue = false;
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ~InspectorFrames() { ImGui::DestroyContext(m_gui); }
        InspectorFrames(const InspectorFrames&) = delete;
        InspectorFrames& operator=(const InspectorFrames&) = delete;

        void Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
            ImGui::SetNextWindowSize(ImVec2{600.0f, 1900.0f});
            m_inspector.Render(m_editor);
            ImGui::EndFrame();
        }

    private:
        LevelEditorController& m_editor;
        NS::Editor::InspectorPanel m_inspector;
        ImGuiContext* m_gui = nullptr;
    };

    // actor の表で part を持つ行
    const NS::ClassObjInfo* RowOf(const NS::SceneActor& actor, const NS::Object* part)
    {
        const NS::ClassObjInfo* found = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& row, const NS::Object* live) {
            if (live == part)
            {
                found = &row;
            }
        });
        return found;
    }

    // インスペクタの、部品 row の欄 fieldName の行の ID。部品は鍵で、欄の表は名前で、欄は並びの番号で ID を積む
    ImGuiID FieldRowId(const NS::ClassObjInfo& row, const NS::Object& part, std::string_view fieldName)
    {
        const ImGuiWindow* window = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
        const NS::ReflectionInfo* info = part.GetReflection();
        if (window == nullptr || info == nullptr)
        {
            return 0;
        }
        int index = -1;
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (fieldName == info->fields[i].name)
            {
                index = static_cast<int>(i);
            }
        }
        ImGuiID id = ImHashStr(row.key, 0, window->ID);
        id = ImHashStr("##fields", 0, id);
        return ImHashData(&index, sizeof(index), id);
    }

    // above から真下へ撃ち、当たった所までの距離。当たらなければ空
    std::optional<float> DownHit(const NS::Scene& scene, const NS::Vector3& above)
    {
        float distance = 0.0f;
        if (!scene.Physics().Raycast(above, -NS::Vector3::UnitY, 20.0f, distance))
        {
            return std::nullopt;
        }
        return distance;
    }
} // namespace

// インスペクタで当たりの形の欄を変えると、世界を止めた歩の後の当たりがその形になる
TEST(EditorInspector, FieldChangeReachesTheCollision)
{
    NS::Scene scene;
    scene.SetSimulationEnabled(false);
    LevelEditorController editor(&scene);
    editor.PlaceItem(*NS::Editor::FindPlacementItem("置物"));
    NS::SceneActor* prop = editor.SelectedObjectActor();
    ASSERT_NE(prop, nullptr);
    ASSERT_EQ(prop->Root().Scale(), NS::Vector3::One);
    const NS::ClassObjInfo* row = RowOf(*prop, prop->CollisionClassObj());
    ASSERT_NE(row, nullptr);
    scene.OnUpdate();

    // 中心から横へ 2 ずらした真上。半径 3 の球なら中心より上に当たる
    const NS::Vector3 above = prop->Root().Position() + NS::Vector3{2.0f, 10.0f, 0.0f};
    const std::optional<float> before = DownHit(scene, above);
    EXPECT_TRUE(!before.has_value() || *before > 10.0f);

    InspectorFrames frames(editor);
    frames.Frame();
    frames.Frame();
    const ImGuiID input = ImHashStr("##value", 0, FieldRowId(*row, *prop->CollisionClassObj(), "半径"));
    ImGui::ActivateItemByID(input);
    GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    frames.Frame();
    ASSERT_EQ(GImGui->ActiveId, input);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8("3");
    frames.Frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frames.Frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frames.Frame();

    scene.OnUpdate();
    const std::optional<float> after = DownHit(scene, above);
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(*after, 10.0f - std::sqrt(5.0f), 0.01f);
}

// インスペクタの上書きの印から既定へ戻すと、描くメッシュも、世界を止めた歩の後のメッシュの当たりも既定の形になる
TEST(EditorInspector, FieldRevertReachesTheMeshAndTheCollision)
{
    NS::Scene scene;
    scene.SetSimulationEnabled(false);
    LevelEditorController editor(&scene);
    editor.PlaceItem(*NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel));
    NS::SceneActor* parts = editor.SelectedObjectActor();
    ASSERT_NE(parts, nullptr);
    NS::Model* model = parts->ModelClassObj();
    ASSERT_NE(model, nullptr);
    const NS::ClassObjInfo* row = RowOf(*parts, model);
    ASSERT_NE(row, nullptr);
    scene.OnUpdate();
    const NS::Mesh* cubeMesh = model->GetMesh();

    // 坂の低い側の真上。立方体の上面と坂の面で距離が違う
    const NS::Vector3 above = parts->Root().Position() + NS::Vector3{0.0f, 2.0f, -0.4f};
    const std::optional<float> cubeHit = DownHit(scene, above);
    ASSERT_TRUE(cubeHit.has_value());

    // 坂にするのは前置き。確かめるのは戻す道
    model->SetMeshRef("wedge45");
    parts->ReloadAssets();
    scene.OnUpdate();
    const std::optional<float> wedgeHit = DownHit(scene, above);
    ASSERT_TRUE(wedgeHit.has_value());
    ASSERT_GT(*wedgeHit, *cubeHit + 0.5f);

    InspectorFrames frames(editor);
    frames.Frame();
    frames.Frame();
    const ImGuiID rowId = FieldRowId(*row, *model, "メッシュ");
    ImGui::ActivateItemByID(ImHashStr("上書き", 0, rowId));
    frames.Frame();
    char popupName[32];
    ImFormatString(popupName, sizeof(popupName), "##Popup_%08x", ImHashStr("##override", 0, rowId));
    const ImGuiWindow* popup = ImGui::FindWindowByName(popupName);
    ASSERT_NE(popup, nullptr);
    ImGui::ActivateItemByID(ImHashStr("既定に戻す", 0, popup->ID));
    frames.Frame();
    frames.Frame();

    EXPECT_EQ(model->GetMesh(), cubeMesh);
    scene.OnUpdate();
    const std::optional<float> after = DownHit(scene, above);
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(*after, *cubeHit, 0.001f);
}

// 種類の既定へ上げると、付いて行くアクターの描くメッシュも、世界を止めた歩の後のメッシュの当たりも上げた形になる
TEST(EditorInspector, FieldPromoteReachesTheFollowersMeshAndCollision)
{
    const ScopedArchetypeDirectory archetypes("EditorFieldPromote");
    NS::Scene scene;
    scene.SetSimulationEnabled(false);
    LevelEditorController editor(&scene);
    const NS::Editor::PlacementItem* cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    ASSERT_NE(cube, nullptr);
    editor.PlaceItem(*cube);
    NS::SceneActor* source = editor.SelectedObjectActor();
    ASSERT_NE(source, nullptr);
    editor.PlaceItem(*cube);
    NS::SceneActor* follower = editor.SelectedObjectActor();
    ASSERT_NE(follower, nullptr);
    ASSERT_NE(follower, source);
    follower->Root().SetPosition(source->Root().Position() + NS::Vector3{5.0f, 0.0f, 0.0f});
    scene.OnUpdate();

    const NS::Vector3 above = follower->Root().Position() + NS::Vector3{0.0f, 2.0f, -0.4f};
    const std::optional<float> cubeHit = DownHit(scene, above);
    ASSERT_TRUE(cubeHit.has_value());

    source->ModelClassObj()->SetMeshRef("wedge45");
    source->ReloadAssets();
    const NS::ClassObjInfo* row = RowOf(*source, source->ModelClassObj());
    ASSERT_NE(row, nullptr);
    ASSERT_TRUE(editor.PromoteFieldToArchetype(*source, *row, "メッシュ"));

    EXPECT_EQ(follower->ModelClassObj()->GetMesh(), source->ModelClassObj()->GetMesh());
    scene.OnUpdate();
    const std::optional<float> after = DownHit(scene, above);
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(*after, *cubeHit + 0.5f);
}

// マテリアルを付けると、描くマテリアルが付けた物になる
TEST(EditorInspector, MaterialApplyReachesTheDrawnMaterial)
{
    NS::ApplicationDesc desc;
    desc.window.title = "マテリアルを付ける試し";
    desc.window.size = {160, 90};
    desc.window.visible = false;
    NS::Application app(desc);
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        NS::Scene scene;
        scene.SetSimulationEnabled(false);
        LevelEditorController editor(&scene);
        editor.PlaceItem(*NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel));
        NS::SceneActor* parts = editor.SelectedObjectActor();
        ASSERT_NE(parts, nullptr);
        const std::string stone = "Assets/Materials/stone.mat";
        ASSERT_TRUE(editor.ApplyMaterialToSelected(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), stone)));
        EXPECT_EQ(parts->ModelClassObj()->DrawnMaterial(), app.Assets().LoadMaterial(stone));
    }));
    EXPECT_EQ(app.Run(), 0);
}
