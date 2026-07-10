#include "Game/Level/LevelObjects.h"
#include "Game/Level/PlayState.h"

#include "Framework/Math/Math.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Scene;

namespace
{
    // 拾得種別だけが異なる object を作る。 種別は components が表すので CRC も components で決まる
    SceneNs::ObjectData MakePickupObject(int pickupKind)
    {
        SceneNs::ObjectData object{};
        object.components.push_back(
            SceneNs::ComponentData{"PickupComponent", {SceneNs::FieldValue{"Pickup Kind", pickupKind}}});
        return object;
    }
} // namespace

TEST(SceneDataCrcTest, EmptyLevelIsDeterministic)
{
    SceneNs::SceneData a, b;
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

// 共有アクセサ FindComponentData / FindField / PickupKindOf の契約 (発見 / 不在 / 既定) を縛る
// Blocks / PlayMode / AutoTile が同じ窓口を読むので、 ここが種別判定の単一の真実になる
TEST(SceneDataAccessors, FindComponentFieldAndPickupKind)
{
    SceneNs::ObjectData goal = MakePickupObject(1);

    const SceneNs::ComponentData* pickup = SceneNs::FindComponentData(goal, "PickupComponent");
    ASSERT_NE(pickup, nullptr);
    EXPECT_EQ(SceneNs::FindComponentData(goal, "BoxColliderComponent"), nullptr); // 不在は nullptr

    ASSERT_NE(SceneNs::FindField(*pickup, "Pickup Kind"), nullptr);
    EXPECT_EQ(SceneNs::FindField(*pickup, "Missing"), nullptr); // 欠損 field は nullptr

    EXPECT_EQ(LevelNs::PickupKindOf(goal), 1);                       // ゴール
    EXPECT_EQ(LevelNs::PickupKindOf(MakePickupObject(0)), 0);        // コイン
    EXPECT_EQ(LevelNs::PickupKindOf(SceneNs::ObjectData{}), -1); // PickupComponent 無し

    // PickupComponent はあるが "Pickup Kind" 欠損 → コイン既定 0
    SceneNs::ObjectData noField{};
    noField.components.push_back(SceneNs::ComponentData{"PickupComponent", {}});
    EXPECT_EQ(LevelNs::PickupKindOf(noField), 0);
}

TEST(SceneDataCrcTest, DifferentComponentsProduceDifferentCrc)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(MakePickupObject(0));
    b.objects.push_back(MakePickupObject(1));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataCrcTest, PlayStateMutationDoesNotAffectSceneDataCrc)
{
    SceneNs::SceneData level;
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    level.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{5.0f, 0.0f, 0.0f}, NS::Math::Quaternion{}));
    const auto before = level.ComputeCrc32();

    LevelNs::PlayState play;
    for (int i = 0; i < 100; ++i)
    {
        play.playerPosition.x += 0.1f;
        play.coinCount += 1;
        play.paused = !play.paused;
    }
    const auto after = level.ComputeCrc32();
    EXPECT_EQ(before, after);
}

TEST(SceneDataCrcTest, ObjectsSizeIsHashed)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    a.objects.push_back(LevelNs::MakeCellObject(1, 0, 0, 0));
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataCrcTest, RotationStepIsHashed)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 1));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataCrcTest, MaterialPathsAreHashed)
{
    SceneNs::SceneData a, b;
    a.materialPaths.push_back("Assets/Materials/Stone.mat");
    b.materialPaths.push_back("Assets/Materials/Grass.mat");
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

// 環境欄は見た目を確定する永続データなので、 差があれば dirty 検知の CRC も必ず動く
TEST(SceneDataCrcTest, EnvironmentIsHashed)
{
    SceneNs::SceneData a, b;
    a.environment.ambientColor.x = 0.1f;
    b.environment.ambientColor.x = 0.9f;
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());

    SceneNs::SceneData c, d;
    c.environment.skyboxCubemapPath = "Assets/Skybox/a/";
    d.environment.skyboxCubemapPath = "Assets/Skybox/b/";
    EXPECT_NE(c.ComputeCrc32(), d.ComputeCrc32());
}

TEST(SceneDataCrcTest, VectorCapacityDoesNotAffectCrc)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    b.objects.reserve(1000);
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(SceneDataComponents, ComponentDataHoldsTypeNameAndFields)
{
    SceneNs::ComponentData cd;
    cd.typeName = "BoxColliderComponent";
    cd.fields.push_back(SceneNs::FieldValue{"Half Extents", NS::Math::Vector3{0.5f, 0.5f, 0.5f}});
    cd.fields.push_back(SceneNs::FieldValue{"Radius", 1.0f});

    EXPECT_EQ(cd.typeName, "BoxColliderComponent");
    ASSERT_EQ(cd.fields.size(), 2u);
    EXPECT_EQ(cd.fields[0].name, "Half Extents");
    EXPECT_EQ(cd.fields[1].name, "Radius");
}

TEST(SceneDataComponents, ObjectDataCopyIsDeep)
{
    SceneNs::ObjectData a{};
    a.components.push_back(SceneNs::ComponentData{"HazardComponent", {}});

    SceneNs::ObjectData b = a;
    b.components.clear();

    EXPECT_EQ(a.components.size(), 1u);
    EXPECT_EQ(b.components.size(), 0u);
}

TEST(SceneDataComponents, Crc32ChangesWhenComponentAdded)
{
    SceneNs::SceneData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));

    SceneNs::ObjectData withComponent = LevelNs::MakeCellObject(0, 0, 0, 0);
    withComponent.components.push_back(SceneNs::ComponentData{"HazardComponent", {}});
    b.objects.push_back(withComponent);

    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}
