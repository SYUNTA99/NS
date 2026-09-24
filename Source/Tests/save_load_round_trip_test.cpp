#include "Editor/LevelFilePaths.h"
#include "Editor/EditorObjects.h"
#include "Game/Level/Breakable.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/KillZone.h"
#include "Game/Player.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;
namespace EditorNs = NS::Editor;

namespace
{
    // テストの吐くシーンは TestOutput/ 配下に隔離する。.gitignore はこの置き場だけを除外する
    std::optional<std::string> TestScenePath(const std::string& name)
    {
        return EditorNs::BuildLevelPath("TestOutput/" + name);
    }

    // 追従先の参照だけを持つ追従カメラの配置物を作る
    [[nodiscard]] NS::Obj::ObjectData MakeFollowCameraObject(std::uint32_t targetObjectId)
    {
        nlohmann::json follow = NS::Obj::MakeComponentEntry("ThirdPersonFollow");
        NS::Obj::SetField(follow, "追従対象", NS::Obj::ObjectRef{targetObjectId});
        NS::Obj::ObjectData object{};
        object.components = nlohmann::json::array({std::move(follow)});
        NS::Obj::EnsureTransformComponent(object);
        return object;
    }
} // namespace

TEST(SaveLoadRoundTrip, SaveAndReloadSemanticEqual)
{
    std::optional<std::string> path = TestScenePath("test_roundtrip");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.0f, 2.0f, 3.0f}, NS::Core::Quaternion{}));
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::ObjectData rotated = NS::Editor::MakeCellObject(1, 0, 1);
    SceneNs::SetObjectRotation(rotated,
                               NS::Core::Quaternion::CreateFromYawPitchRoll(NS::Core::k_Pi * 0.5f, 0.0f, 0.0f));
    src.objects.push_back(rotated);
    src.objects.push_back(NS::Editor::MakeCellObject(2, 0, 0));
    // 編集中のレベルは読込採番か Command 採番で常に id を持つため、比べる元も採番後から取る
    SceneNs::EnsureUniqueObjectIds(src);

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));
    EXPECT_TRUE(dst == src);
}

TEST(SaveLoadRoundTrip, DisabledComponentSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);

    nlohmann::json* entry = SceneNs::FindComponentEntry(src.objects[0], "MeshRenderer");
    ASSERT_NE(entry, nullptr);
    SceneNs::SetComponentEntryEnabled(*entry, false);

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 1u);
    const nlohmann::json* reloaded = SceneNs::FindComponentEntry(dst.objects[0], "MeshRenderer");
    ASSERT_NE(reloaded, nullptr);
    EXPECT_FALSE(SceneNs::ComponentEntryEnabled(*reloaded))
        << "切った component が読み直しで戻る。保存側は書き出すので、編集で切っても開くたびに復活する";
}

TEST(SaveLoadRoundTrip, ObjectNameSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects[0].name = "足場A";
    const SceneNs::SceneData named = src;

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_EQ(dst.objects[0].name, "足場A");
    EXPECT_TRUE(dst == named);

    // 名前が operator== から抜けると、往復で消えても等しいと判定される
    src.objects[0].name.clear();
    EXPECT_FALSE(src == named);
}

TEST(SaveLoadRoundTrip, ObjectActiveSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    const SceneNs::SceneData enabled = src;

    src.objects[0].active = false;
    const SceneNs::SceneData disabled = src;
    // 有効かどうかは比較対象
    EXPECT_FALSE(disabled == enabled);

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_FALSE(dst.objects[0].active);
    EXPECT_TRUE(dst == disabled);
}

TEST(SaveLoadRoundTrip, ObjectSequenceSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    src.objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    const std::uint32_t first = src.objects[0].objectId;
    const std::uint32_t second = src.objects[1].objectId;

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.objects[0].objectId, first);
    EXPECT_EQ(dst.objects[1].objectId, second);
}

TEST(SaveLoadRoundTrip, MissingActiveReadsAsDefault)
{
    // 欄を持たない古いファイルが従来どおり読めること
    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string text = SceneNs::SerializeSceneToJson(src);
    const nlohmann::json json = nlohmann::json::parse(text);
    const nlohmann::json& first = json.at("objects").at(0);
    // 既定値は書かない。欄が増えても古いファイルとバイト互換が保てる
    EXPECT_TRUE(first.find("active") == first.end());

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, text));
    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_TRUE(dst.objects[0].active);
}

TEST(SaveLoadRoundTrip, ObjectParentSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    src.objects.push_back(NS::Editor::MakeCellObject(1, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects[1].parentId = src.objects[0].objectId;
    const SceneNs::SceneData parented = src;

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.objects[0].parentId, SceneNs::k_NoObjectId);
    EXPECT_EQ(dst.objects[1].parentId, dst.objects[0].objectId);
    EXPECT_TRUE(dst == parented);

    // 親子も比較対象
    src.objects[1].parentId = SceneNs::k_NoObjectId;
    EXPECT_FALSE(src == parented);
}

// 正準 JSON は object キーが辞書順・float が最短往復表現なので、同一データの 2 回保存は
// バイト一致する。保存し直しただけでは git diff に行が出ない
TEST(SaveLoadRoundTrip, TwoSavesAreByteIdentical)
{
    std::optional<std::string> path1 = TestScenePath("test_byteid_a");
    std::optional<std::string> path2 = TestScenePath("test_byteid_b");
    ASSERT_TRUE(path1);
    ASSERT_TRUE(path2);

    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(5, 5, 5));

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path1));
    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path2));

    std::optional<std::vector<std::byte>> b1 = NS::Platform::FileSystem::ReadAllBytes(*path1);
    std::optional<std::vector<std::byte>> b2 = NS::Platform::FileSystem::ReadAllBytes(*path2);
    ASSERT_TRUE(b1.has_value());
    ASSERT_TRUE(b2.has_value());
    ASSERT_EQ(b1->size(), b2->size());
    EXPECT_EQ(std::memcmp(b1->data(), b2->data(), b1->size()), 0);
}

TEST(SaveLoadRoundTrip, LoadCorruptedFileFallsBackToEmpty)
{
    std::optional<std::string> path = TestScenePath("test_corrupted");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    std::optional<std::vector<std::byte>> bytes = NS::Platform::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GE(bytes->size(), 1u);
    // 先頭の '{' を壊すと JSON parse が失敗し、load は false + 空 SceneData を返す
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(*bytes)));

    SceneNs::SceneData dst;
    EXPECT_FALSE(SceneNs::LoadSceneFromJsonFile(dst, *path));
    EXPECT_TRUE(dst.objects.empty());
}

// object 数が上限を超えるレベルは保存段でクラッシュせず false を返す。メモリ枯渇まで走らせない
TEST(SaveLoadRoundTrip, RejectsOversizedObjectCount)
{
    std::optional<std::string> path = TestScenePath("test_oversized");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData huge;
    huge.objects.resize(100'001); // 上限 100'000 を 1 件超過させる

    EXPECT_FALSE(SceneNs::SaveSceneToJsonFile(huge, *path));
}

// 型名 + リフレクションフィールド値 (値 5 つ) を持つ component 一覧が save→load で復元される
// 並びは正準化 (名前昇順) されるので、等価判定は正準 JSON の一致で行う
TEST(SaveLoadRoundTrip, ComponentsRoundTrip)
{
    std::optional<std::string> path = TestScenePath("test_components");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    SceneNs::ObjectData freeObject{};
    SceneNs::SetObjectPosition(freeObject, NS::Core::Vector3{1.5f, 0.0f, 0.0f});

    nlohmann::json comp = SceneNs::MakeComponentEntry("BoxCollider");
    SceneNs::SetField(comp, "vHalf", NS::Core::Vector3{1.0f, 2.0f, 3.0f});
    SceneNs::SetField(comp, "iCount", 7);
    SceneNs::SetField(comp, "bOn", true);
    SceneNs::SetField(comp, "fSpeed", 1.5f);
    SceneNs::SetField(comp, "fWhole", 4.0f); // 整数値の float が int に化けないことを確かめる
    freeObject.components.push_back(std::move(comp));
    src.objects.push_back(std::move(freeObject));
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // 正準 JSON 同士の比較なので、読込側と同じく採番済の状態に揃えてから保存する
    SceneNs::EnsureUniqueObjectIds(src);

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));

    EXPECT_EQ(SceneNs::SerializeSceneToJson(dst), SceneNs::SerializeSceneToJson(src));

    ASSERT_EQ(dst.objects.size(), 3u);
    // freeObject は BoxCollider と TransformComponent の 2 つを持つ
    ASSERT_EQ(dst.objects[0].components.size(), 2u);
    const nlohmann::json* box = SceneNs::FindComponentEntry(dst.objects[0], "BoxCollider");
    ASSERT_NE(box, nullptr);
    const nlohmann::json& fields = box->at("fields");
    ASSERT_EQ(fields.size(), 5u);

    EXPECT_FLOAT_EQ(SceneNs::FieldVector3(*box, "vHalf", {}).y, 2.0f);

    ASSERT_TRUE(fields.at("iCount").is_number_integer());
    EXPECT_EQ(SceneNs::FieldInt(*box, "iCount", -1), 7);

    ASSERT_TRUE(fields.at("bOn").is_boolean());
    EXPECT_TRUE(fields.at("bOn").get<bool>());

    ASSERT_TRUE(fields.at("fSpeed").is_number_float());
    EXPECT_FLOAT_EQ(SceneNs::FieldFloat(*box, "fSpeed", -1.0f), 1.5f);

    ASSERT_TRUE(fields.at("fWhole").is_number_float()); // 整数値でも float のまま
    EXPECT_FLOAT_EQ(SceneNs::FieldFloat(*box, "fWhole", -1.0f), 4.0f);
}

TEST(SaveLoadRoundTrip, BuildLevelPathRejectsTraversal)
{
    // 上位フォルダへの抜け出しは呼ぶ側でなく path を組む所で止まる
    EXPECT_FALSE(EditorNs::BuildLevelPath("../etc/passwd").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("..").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("a/../b").has_value());
    // サブフォルダ区切りは有効
    EXPECT_TRUE(EditorNs::BuildLevelPath("a/b").has_value());
}

// 配置物 (ObjectData) の transform と className が保存・再読込の往復で戻る
TEST(SaveLoadRoundTrip, ObjectsRoundTrip)
{
    std::optional<std::string> path = TestScenePath("test_objects_roundtrip");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;

    SceneNs::ObjectData freeObject{};
    SceneNs::SetObjectPosition(freeObject, NS::Core::Vector3{1.5f, 2.25f, -3.75f});
    SceneNs::SetObjectRotation(freeObject, NS::Core::Quaternion{0.0f, 0.70710677f, 0.0f, 0.70710677f});
    SceneNs::SetObjectScale(freeObject, NS::Core::Vector3{2.0f, 0.5f, 1.0f});
    src.objects.push_back(freeObject);

    SceneNs::ObjectData gridObject{};
    // 読込は全 object に transform を保証するため、比べる元を合わせるよう src 側にも 1 つ持たせる
    SceneNs::EnsureTransformComponent(gridObject);
    src.objects.push_back(gridObject);
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));

    // 編集中のレベルは常に採番済なので、比べる元も採番済から取る
    SceneNs::EnsureUniqueObjectIds(src);
    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));
    EXPECT_TRUE(dst == src);

    ASSERT_EQ(dst.objects.size(), 4u);

    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(dst.objects[0]).x, 1.5f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(dst.objects[0]).z, -3.75f);
    EXPECT_NEAR(SceneNs::ObjectRotation(dst.objects[0]).w, 0.70710677f, 1e-5f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectScale(dst.objects[0]).x, 2.0f);

    // クラス名は書いた object だけ載って戻り、素の object は空のまま
    EXPECT_EQ(dst.objects[0].className, "");
    EXPECT_EQ(dst.objects[2].className, "Player");
}

// 配置物の "基本色" リフレクション値が save→reload を往復で保持される
// 種別固定の色上書きが消え、色は component 経由で保存に残る
TEST(SaveLoadRoundTrip, BaseColorSurvivesRoundTrip)
{
    std::optional<std::string> path = TestScenePath("test_basecolor");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    SceneNs::ObjectData solid = NS::Editor::MakeCellObject(0, 0, 0);
    const NS::Core::Vector3 baseColor{0.2f, 0.6f, 0.9f};
    for (nlohmann::json& component : solid.components)
        if (SceneNs::HasField(component, "基本色"))
            SceneNs::SetField(component, "基本色", baseColor);
    src.objects.push_back(solid);
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));

    ASSERT_EQ(dst.objects.size(), 2u);
    bool found = false;
    for (const nlohmann::json& component : dst.objects[0].components)
    {
        if (!SceneNs::HasField(component, "基本色"))
            continue;
        const NS::Core::Vector3 v = SceneNs::FieldVector3(component, "基本色", {});
        EXPECT_FLOAT_EQ(v.x, 0.2f);
        EXPECT_FLOAT_EQ(v.y, 0.6f);
        EXPECT_FLOAT_EQ(v.z, 0.9f);
        found = true;
    }
    EXPECT_TRUE(found) << "Base Color が往復で消えた";
}

// プレイヤー実体の姿勢が保存・再読込の往復で保たれる。読込は object を足さない
TEST(SaveLoadRoundTrip, PlayerObjectRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.25f, 3.5f, -2.75f},
                                           NS::Core::Quaternion{0.0f, 0.70710677f, 0.0f, 0.70710677f}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 1u);
    const std::size_t playerIndex = FindPlayerObjectIndex(dst);
    ASSERT_NE(playerIndex, SceneNs::k_NoObjectIndex);
    const SceneNs::ObjectData& loaded = dst.objects[playerIndex];
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(loaded).x, 1.25f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(loaded).y, 3.5f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(loaded).z, -2.75f);
    EXPECT_NEAR(SceneNs::ObjectRotation(loaded).y, 0.70710677f, 1e-5f);
    EXPECT_NEAR(SceneNs::ObjectRotation(loaded).w, 0.70710677f, 1e-5f);
}

// 補完の呼び手はエディタのレベル読込だけ
TEST(EnsurePlayableObjects, SynthesizesPlayerAndKillZone)
{
    SceneNs::SceneData level;
    EXPECT_TRUE(EnsurePlayerObject(level));
    EXPECT_TRUE(LevelNs::EnsureKillZoneObject(level));

    const std::size_t playerIndex = FindPlayerObjectIndex(level);
    ASSERT_NE(playerIndex, SceneNs::k_NoObjectIndex);
    const SceneNs::ObjectData& player = level.objects[playerIndex];
    EXPECT_NE(player.objectId, 0u); // 合成後の一意化で永続 id も振られる
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(player).y, 1.41f);
    // 既定構成のうち 5 つ
    EXPECT_NE(SceneNs::FindComponentEntry(player, "MeshRenderer"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "PlayerComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "PlayerInput"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "Health"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "Shadow"), nullptr);

    // 落下死体積も 1 つ敷かれる
    bool hasKillZone = false;
    for (const SceneNs::ObjectData& object : level.objects)
    {
        if (LevelNs::IsKillZoneObject(object))
            hasKillZone = true;
    }
    EXPECT_TRUE(hasKillZone);
}

// プレイヤーが複数居ても先頭を正とする。手編集の重複でも読込と補完は成立する
TEST(SaveLoadRoundTrip, MultiplePlayersFirstWins)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, NS::Core::Quaternion{}));
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{9.0f, 0.0f, 0.0f}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_FALSE(EnsurePlayerObject(dst)); // プレイヤーが居るので合成しない
    const std::size_t playerIndex = FindPlayerObjectIndex(dst);
    ASSERT_EQ(playerIndex, 0u);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(dst.objects[playerIndex]).x, 1.0f);
}

// environment 欄が save→load で往復する。照明は Component へ移り、ここに残るのは skybox
TEST(SaveLoadRoundTrip, EnvironmentRoundTrip)
{
    SceneNs::SceneData src;
    src.environment.skyboxCubemapPath = "Assets/Skybox/kurt/";
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    EXPECT_EQ(dst.environment.skyboxCubemapPath, "Assets/Skybox/kurt/");
    EXPECT_TRUE(dst == src);
}

// 旧版のファイルは黙って既定値で読まず、読込自体を拒否する。欄名が違う旧データの静かな破壊を防ぐ
TEST(SaveLoadRoundTrip, RejectsOldFormatVersion)
{
    const std::string json = R"({ "version": 2, "objects": [] })";
    SceneNs::SceneData dst;
    EXPECT_FALSE(SceneNs::DeserializeSceneFromJson(dst, json));
}

// 旧形式の照明キーは照明が Component へ移った今、読み飛ばされる。skybox だけが environment から読める
TEST(SaveLoadRoundTrip, LegacyLightingKeysAreIgnored)
{
    const std::string json = R"({
        "version": 4,
        "objects": [],
        "environment": { "skybox": "Assets/Skybox/kurt/", "lightColor": [0.5, 0.6, 0.7] }
    })";

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    // skybox は読める。旧照明キー lightColor は無視され、読込は壊れない
    EXPECT_EQ(dst.environment.skyboxCubemapPath, "Assets/Skybox/kurt/");
}

// type が数値の component も空の型名で読み、読込を止めない。例外を切っているので型を確かめずに読むと異常終了する
TEST(SaveLoadRoundTrip, NonStringComponentTypeReadsAsEmpty)
{
    const std::string json = R"({
        "version": 4,
        "objects": [ { "id": 1, "components": [ { "type": 5, "fields": {} } ] } ]
    })";

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));
    ASSERT_EQ(dst.objects.size(), 1u);
    ASSERT_FALSE(dst.objects[0].components.empty());
    EXPECT_EQ(SceneNs::ComponentEntryType(dst.objects[0].components[0]), "");
}

// 追従カメラの配置物は Target 参照ごと往復で保持される
TEST(SaveLoadRoundTrip, FollowCameraObjectRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_TRUE(dst == src);
    const nlohmann::json* comp = SceneNs::FindComponentEntry(dst.objects[1], "ThirdPersonFollow");
    ASSERT_NE(comp, nullptr);
    ASSERT_TRUE(SceneNs::HasField(*comp, "追従対象"));
    EXPECT_EQ(SceneNs::FieldObjectRef(*comp, "追従対象").id, dst.objects[0].objectId);
}

namespace
{
    // Cube 1 個へ耐久を積む。値は欄名をキーに書き、Inspector で入れた時と同じ形にする
    SceneNs::ObjectData MakeBreakableCube(int cellX, float toughness)
    {
        SceneNs::ObjectData object = NS::Editor::MakeCellObject(cellX, 0, 0);
        nlohmann::json breakable = SceneNs::MakeComponentEntry("Breakable");
        SceneNs::SetField(breakable, "耐久", toughness);
        object.components.push_back(std::move(breakable));
        return object;
    }

    const nlohmann::json* FindBreakableEntry(const nlohmann::json& components)
    {
        for (const nlohmann::json& entry : components)
        {
            if (entry.value("type", std::string{}) == "Breakable")
                return &entry;
        }
        return nullptr;
    }

    nlohmann::json* FindBreakableEntry(nlohmann::json& components)
    {
        for (nlohmann::json& entry : components)
        {
            if (entry.value("type", std::string{}) == "Breakable")
                return &entry;
        }
        return nullptr;
    }
} // namespace

// 入れた耐久が JSON を経て live の Component まで戻る
TEST(SaveLoadRoundTrip, BreakableValuesSurviveRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakeBreakableCube(0, 2.0f));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));
    ASSERT_EQ(dst.objects.size(), 1u);

    const nlohmann::json* entry = SceneNs::FindComponentEntry(dst.objects[0], "Breakable");
    ASSERT_NE(entry, nullptr);
    EXPECT_FLOAT_EQ(SceneNs::FieldFloat(*entry, "耐久", -1.0f), 2.0f);

    const std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(dst.objects[0], nullptr);
    ASSERT_NE(live, nullptr);
    const LevelNs::Breakable* breakable = live->FindComponent<LevelNs::Breakable>();
    ASSERT_NE(breakable, nullptr);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 2.0f);
}

// 同じ Cube を 2 個並べても値は個体ごと
TEST(SaveLoadRoundTrip, BreakableValuesStayPerObject)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakeBreakableCube(0, 1.0f));
    src.objects.push_back(MakeBreakableCube(1, 3.0f));
    SceneNs::EnsureUniqueObjectIds(src);

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));
    ASSERT_EQ(dst.objects.size(), 2u);

    const std::unique_ptr<SceneNs::GameObject> light = SceneNs::BuildSceneObject(dst.objects[0], nullptr);
    const std::unique_ptr<SceneNs::GameObject> heavy = SceneNs::BuildSceneObject(dst.objects[1], nullptr);
    ASSERT_NE(light, nullptr);
    ASSERT_NE(heavy, nullptr);

    const LevelNs::Breakable* lightBreakable = light->FindComponent<LevelNs::Breakable>();
    const LevelNs::Breakable* heavyBreakable = heavy->FindComponent<LevelNs::Breakable>();
    ASSERT_NE(lightBreakable, nullptr);
    ASSERT_NE(heavyBreakable, nullptr);
    EXPECT_FLOAT_EQ(lightBreakable->Toughness(), 1.0f);
    EXPECT_FLOAT_EQ(heavyBreakable->Toughness(), 3.0f);
}

// .scene のキーは欄名そのもの。保存と読込が同じ欄名を使う限り往復自体は通るので、
// 綴りは値ごと突き合わせる。取り違えは保存済みレベルの値を静かに既定へ戻す
TEST(SaveLoadRoundTrip, BreakableFieldKeysAreTheLockedLabels)
{
    SceneNs::GameObject live;
    LevelNs::Breakable* breakable = live.AddComponent<LevelNs::Breakable>();
    ASSERT_NE(breakable, nullptr);
    breakable->SetToughness(1.5f);

    SceneNs::SceneData src;
    src.objects.push_back(SceneNs::CaptureObjectData(live));

    const nlohmann::json root = nlohmann::json::parse(SceneNs::SerializeSceneToJson(src));
    const nlohmann::json* entry = FindBreakableEntry(root.at("objects").at(0).at("components"));
    ASSERT_NE(entry, nullptr);

    const nlohmann::json& fields = entry->at("fields");
    ASSERT_TRUE(fields.contains("耐久")) << "欄名を変えると保存済みレベルの耐久が既定へ戻る";
    EXPECT_FLOAT_EQ(fields.at("耐久").get<float>(), 1.5f);
    // 質量は RigidBody の欄。Breakable に書くと同じ重さの出所が 2 つになる
    EXPECT_EQ(fields.size(), 1u);
}

// 欄が欠けた .scene でも読込は壊れない。欠けた欄はコード既定へ落ちる
TEST(SaveLoadRoundTrip, MissingBreakableFieldFallsBackToDefault)
{
    SceneNs::GameObject source;
    LevelNs::Breakable* authored = source.AddComponent<LevelNs::Breakable>();
    ASSERT_NE(authored, nullptr);
    authored->SetToughness(2.0f);

    SceneNs::SceneData src;
    src.objects.push_back(SceneNs::CaptureObjectData(source));

    nlohmann::json root = nlohmann::json::parse(SceneNs::SerializeSceneToJson(src));
    nlohmann::json* entry = FindBreakableEntry(root.at("objects").at(0).at("components"));
    ASSERT_NE(entry, nullptr);
    entry->at("fields").erase("耐久");

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, root.dump()));
    ASSERT_EQ(dst.objects.size(), 1u);

    const std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(dst.objects[0], nullptr);
    ASSERT_NE(live, nullptr);
    const LevelNs::Breakable* breakable = live->FindComponent<LevelNs::Breakable>();
    ASSERT_NE(breakable, nullptr);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 1.0f);
}

namespace
{
    // CollisionInput は調整値の公開 setter を持たないため、Inspector と同じリフレクション経路で読み書きする
    const SceneNs::FieldDesc* ChargeField(const char* label)
    {
        return SceneNs::FindField(LevelNs::CollisionInput::StaticReflection(), label);
    }

    template <class T> void WriteChargeField(LevelNs::CollisionInput& input, const char* label, const T& value)
    {
        const SceneNs::FieldDesc* field = ChargeField(label);
        ASSERT_NE(field, nullptr) << label;
        field->set(&input, &value);
    }

    template <class T> [[nodiscard]] T ReadChargeField(const LevelNs::CollisionInput& input, const char* label)
    {
        T value{};
        const SceneNs::FieldDesc* field = ChargeField(label);
        if (field == nullptr)
        {
            ADD_FAILURE() << label << " の欄が見つからない";
            return value;
        }
        field->get(&input, &value);
        return value;
    }

    const nlohmann::json* FindChargeEntry(const nlohmann::json& components)
    {
        for (const nlohmann::json& entry : components)
        {
            if (entry.value("type", std::string{}) == "CollisionInput")
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] SceneNs::Curve ThreePointCurve()
    {
        SceneNs::Curve curve;
        curve.count = 3;
        curve.keys[0] = SceneNs::Curve::Key{0.0f, 1.0f};
        curve.keys[1] = SceneNs::Curve::Key{0.5f, 1.5f};
        curve.keys[2] = SceneNs::Curve::Key{1.0f, 3.0f};
        return curve;
    }
} // namespace

TEST(SaveLoadRoundTrip, ChargeSecondsSurviveRoundTrip)
{
    SceneNs::GameObject source;
    LevelNs::CollisionInput* authored = source.AddComponent<LevelNs::CollisionInput>();
    ASSERT_NE(authored, nullptr);
    WriteChargeField(*authored, "チャージしきい値秒", 0.4f);
    WriteChargeField(*authored, "チャージ満タン秒", 1.8f);

    SceneNs::SceneData src;
    src.objects.push_back(SceneNs::CaptureObjectData(source));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));
    ASSERT_EQ(dst.objects.size(), 1u);

    const std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(dst.objects[0], nullptr);
    ASSERT_NE(live, nullptr);
    const LevelNs::CollisionInput* input = live->FindComponent<LevelNs::CollisionInput>();
    ASSERT_NE(input, nullptr);
    EXPECT_FLOAT_EQ(ReadChargeField<float>(*input, "チャージしきい値秒"), 0.4f);
    EXPECT_FLOAT_EQ(ReadChargeField<float>(*input, "チャージ満タン秒"), 1.8f);
}

TEST(SaveLoadRoundTrip, ChargeCurveSurvivesRoundTrip)
{
    SceneNs::GameObject source;
    LevelNs::CollisionInput* authored = source.AddComponent<LevelNs::CollisionInput>();
    ASSERT_NE(authored, nullptr);
    WriteChargeField(*authored, "チャージ倍率カーブ", ThreePointCurve());

    SceneNs::SceneData src;
    src.objects.push_back(SceneNs::CaptureObjectData(source));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));
    ASSERT_EQ(dst.objects.size(), 1u);

    const std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(dst.objects[0], nullptr);
    ASSERT_NE(live, nullptr);
    const LevelNs::CollisionInput* input = live->FindComponent<LevelNs::CollisionInput>();
    ASSERT_NE(input, nullptr);

    const SceneNs::Curve loaded = ReadChargeField<SceneNs::Curve>(*input, "チャージ倍率カーブ");
    ASSERT_EQ(loaded.count, 3u);
    EXPECT_FLOAT_EQ(loaded.keys[0].x, 0.0f);
    EXPECT_FLOAT_EQ(loaded.keys[0].y, 1.0f);
    EXPECT_FLOAT_EQ(loaded.keys[1].x, 0.5f);
    EXPECT_FLOAT_EQ(loaded.keys[1].y, 1.5f);
    EXPECT_FLOAT_EQ(loaded.keys[2].x, 1.0f);
    EXPECT_FLOAT_EQ(loaded.keys[2].y, 3.0f);
}

TEST(SaveLoadRoundTrip, BreakFlagSurvivesRoundTrip)
{
    SceneNs::GameObject source;
    LevelNs::ImpactResolver* authored = source.AddComponent<LevelNs::ImpactResolver>();
    ASSERT_NE(authored, nullptr);
    const SceneNs::FieldDesc* field =
        SceneNs::FindField(LevelNs::ImpactResolver::StaticReflection(), "破壊を許可");
    ASSERT_NE(field, nullptr);
    const bool enabled = true;
    field->set(authored, &enabled);

    SceneNs::SceneData src;
    src.objects.push_back(SceneNs::CaptureObjectData(source));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));
    ASSERT_EQ(dst.objects.size(), 1u);

    const std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(dst.objects[0], nullptr);
    ASSERT_NE(live, nullptr);
    const LevelNs::ImpactResolver* impact = live->FindComponent<LevelNs::ImpactResolver>();
    ASSERT_NE(impact, nullptr);
    bool loaded = false;
    field->get(impact, &loaded);
    EXPECT_TRUE(loaded);
}

// キーの綴りを名指しで固定するのは、欄名を後から変えると保存済みレベルの値が静かに既定へ戻るため
TEST(SaveLoadRoundTrip, ChargeFieldKeysAreTheLockedLabels)
{
    SceneNs::GameObject live;
    LevelNs::CollisionInput* input = live.AddComponent<LevelNs::CollisionInput>();
    ASSERT_NE(input, nullptr);
    WriteChargeField(*input, "チャージ倍率カーブ", ThreePointCurve());

    SceneNs::SceneData src;
    src.objects.push_back(SceneNs::CaptureObjectData(live));

    const nlohmann::json root = nlohmann::json::parse(SceneNs::SerializeSceneToJson(src));
    const nlohmann::json* entry = FindChargeEntry(root.at("objects").at(0).at("components"));
    ASSERT_NE(entry, nullptr);

    const nlohmann::json& fields = entry->at("fields");
    EXPECT_TRUE(fields.contains("チャージしきい値秒"));
    EXPECT_TRUE(fields.contains("チャージ満タン秒"));
    EXPECT_TRUE(fields.contains("チャージ倍率カーブ"));
    EXPECT_TRUE(fields.contains("突進位置係数カーブ"));

    const nlohmann::json& points = fields.at("チャージ倍率カーブ").at("curve");
    ASSERT_TRUE(points.is_array());
    ASSERT_EQ(points.size(), 3u);
    for (const nlohmann::json& point : points)
    {
        ASSERT_TRUE(point.is_array());
        EXPECT_EQ(point.size(), 2u);
    }
}

// 参照はファイルに相手の名前で書き、読込で id へ戻す
TEST(SaveLoadRoundTrip, ObjectRefIsWrittenByName)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    src.objects[0].name = "Hero";
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    EXPECT_NE(json.find("\"ref\": \"Hero\""), std::string::npos) << json;

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));
    EXPECT_TRUE(dst == src);
}
