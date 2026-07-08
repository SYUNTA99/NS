#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include "Framework/Math/Math.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;

namespace
{
    // 拾得種別だけが異なる object を作る。 種別は components が表すので CRC も components で決まる
    LevelNs::ObjectInstance MakePickupObject(int pickupKind)
    {
        LevelNs::ObjectInstance object{};
        object.components.push_back(
            LevelNs::ComponentData{"PickupComponent", {LevelNs::FieldValue{"Pickup Kind", pickupKind}}});
        return object;
    }
} // namespace

TEST(LevelDataCrcTest, EmptyLevelIsDeterministic)
{
    LevelNs::LevelData a, b;
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

// 共有アクセサ FindComponentData / FindField / PickupKindOf の契約 (発見 / 不在 / 既定) を縛る
// Blocks / PlayMode / AutoTile が同じ窓口を読むので、 ここが種別判定の単一の真実になる
TEST(LevelDataAccessors, FindComponentFieldAndPickupKind)
{
    LevelNs::ObjectInstance goal = MakePickupObject(1);

    const LevelNs::ComponentData* pickup = LevelNs::FindComponentData(goal, "PickupComponent");
    ASSERT_NE(pickup, nullptr);
    EXPECT_EQ(LevelNs::FindComponentData(goal, "BoxColliderComponent"), nullptr); // 不在は nullptr

    ASSERT_NE(LevelNs::FindField(*pickup, "Pickup Kind"), nullptr);
    EXPECT_EQ(LevelNs::FindField(*pickup, "Missing"), nullptr); // 欠損 field は nullptr

    EXPECT_EQ(LevelNs::PickupKindOf(goal), 1);                       // ゴール
    EXPECT_EQ(LevelNs::PickupKindOf(MakePickupObject(0)), 0);        // コイン
    EXPECT_EQ(LevelNs::PickupKindOf(LevelNs::ObjectInstance{}), -1); // PickupComponent 無し

    // PickupComponent はあるが "Pickup Kind" 欠損 → コイン既定 0
    LevelNs::ObjectInstance noField{};
    noField.components.push_back(LevelNs::ComponentData{"PickupComponent", {}});
    EXPECT_EQ(LevelNs::PickupKindOf(noField), 0);
}

TEST(LevelDataCrcTest, DifferentComponentsProduceDifferentCrc)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(MakePickupObject(0));
    b.objects.push_back(MakePickupObject(1));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, PlayStateMutationDoesNotAffectLevelDataCrc)
{
    LevelNs::LevelData level;
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

TEST(LevelDataCrcTest, ObjectsSizeIsHashed)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    a.objects.push_back(LevelNs::MakeCellObject(1, 0, 0, 0));
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, RotationStepIsHashed)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 1));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, MaterialPathsAreHashed)
{
    LevelNs::LevelData a, b;
    a.materialPaths.push_back("Assets/Materials/Stone.mat");
    b.materialPaths.push_back("Assets/Materials/Grass.mat");
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, MetadataFieldsAreHashed)
{
    LevelNs::LevelData a, b;
    a.bgmId = 1;
    b.bgmId = 2;
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

// 環境欄は見た目を確定する永続データなので、 差があれば dirty 検知の CRC も必ず動く
TEST(LevelDataCrcTest, EnvironmentIsHashed)
{
    LevelNs::LevelData a, b;
    a.environment.ambientColor.x = 0.1f;
    b.environment.ambientColor.x = 0.9f;
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());

    LevelNs::LevelData c, d;
    c.environment.skyboxCubemapPath = "Assets/Skybox/a/";
    d.environment.skyboxCubemapPath = "Assets/Skybox/b/";
    EXPECT_NE(c.ComputeCrc32(), d.ComputeCrc32());
}

TEST(LevelDataCrcTest, VectorCapacityDoesNotAffectCrc)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    b.objects.reserve(1000);
    b.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataComponents, ComponentDataHoldsTypeNameAndFields)
{
    LevelNs::ComponentData cd;
    cd.typeName = "BoxColliderComponent";
    cd.fields.push_back(LevelNs::FieldValue{"Half Extents", NS::Math::Vector3{0.5f, 0.5f, 0.5f}});
    cd.fields.push_back(LevelNs::FieldValue{"Radius", 1.0f});

    EXPECT_EQ(cd.typeName, "BoxColliderComponent");
    ASSERT_EQ(cd.fields.size(), 2u);
    EXPECT_EQ(cd.fields[0].name, "Half Extents");
    EXPECT_EQ(cd.fields[1].name, "Radius");
}

TEST(LevelDataComponents, ObjectInstanceCopyIsDeep)
{
    LevelNs::ObjectInstance a{};
    a.components.push_back(LevelNs::ComponentData{"HazardComponent", {}});

    LevelNs::ObjectInstance b = a;
    b.components.clear();

    EXPECT_EQ(a.components.size(), 1u);
    EXPECT_EQ(b.components.size(), 0u);
}

TEST(LevelDataComponents, Crc32ChangesWhenComponentAdded)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));

    LevelNs::ObjectInstance withComponent = LevelNs::MakeCellObject(0, 0, 0, 0);
    withComponent.components.push_back(LevelNs::ComponentData{"HazardComponent", {}});
    b.objects.push_back(withComponent);

    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}
